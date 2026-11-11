#!/usr/bin/env python3
r"""Windows parity helpers for Backyard 8 (D3D12, D3D11, GL, Vulkan).

Runs on Linux and drives the Windows host over SSH/SCP. Every subcommand holds the shared
Windows lock (``flock``) because the machine is one shared resource.

  python3 scripts/validation/windows_parity.py sync  [--tree <path>] [--revision <ref>]
  python3 scripts/validation/windows_parity.py build [--action Rebuild|Build]
  python3 scripts/validation/windows_parity.py run <script.ps1> <tag> [--timeout <s>]

``build`` defaults to ``-Action Rebuild`` and fails unless ``DayScene.exe`` postdates the
build start. A plain ``-Action Build`` is only safe once a rebuild from the synced C++ has
happened; old objects are newer than the synced sources would suggest unless the sync sets
timestamps (it does: every file whose content changed gets the host's current time).

Host and root come from ``--host``/``--root`` or from ``T850_WIN_HOST`` and
``T850_WIN_ROOT``. There are no defaults in the repository.

``run`` copies ``script.ps1`` to ``<root>\__parity\<tag>\``, starts it in session 1 through
``<root>\start_task.ps1`` (the SSH session is session 0, where DXGI cannot create a
swapchain), polls for ``<root>\__parity\<tag>\DONE.txt``, then fetches that folder to
``/tmp/winport/<tag>/``. A script that fails must still write ``DONE.txt`` (with
``ERROR.txt`` next to it) so the poll ends; the fetch happens either way.

Sync method: the source tree on the host is an archive extraction, not a git checkout. It
lives under ``<root>\T850``, which is also the path of its own ``.git`` directory, so a
plain ``tar -x`` cannot replace tracked files there. ``sync`` therefore
1. builds a file list from ``git ls-files`` (HEAD or ``--tree``),
2. tars those files plus the git link for the vcpkg submodule, on Linux,
3. uploads the archive, and
4. on Windows copies each listed file over the existing tracked file and adds files that
   are new, stamping only those (content differs or absent) with the host's current time so
   the incremental build sees them; unchanged files keep their timestamps. With ``--prune``
   it also deletes tracked files that the new revision removed. The ``.git`` directory,
   ``bin``, ``Librerias/vcpkg`` and every untracked asset stay as they are.
"""
import argparse
import fnmatch
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]          # T850
REPO = PROJECT.parent
LOCK = Path("/tmp/winport/windows.lock")
SCRATCH = "/tmp/opencode/winparity"
SSH_TIMEOUT = 300          # a single SSH round trip, not a whole remote step
BUILD_TIMEOUT = 4 * 3600   # Rebuild compiles the whole solution, Dawn aside
SCP_TIMEOUT = 1800         # a matrix folder can be a few hundred MB
SSH_OPTS = [
    "-o", "BatchMode=yes",
    "-o", "ServerAliveInterval=30",
    "-o", "ServerAliveCountMax=6",
    "-o", "ConnectTimeout=20",
]

# Subtrees synced in full (a prefix match on the repo-relative path).
FULL_SUBTREES = (
    "T850/Framework/",
    "T850/FrameworkImGui/",
    "T850/DayScene/",
    "T850/T8ditor/",
    "T850/scripts/",
    "T850/Assets/Shaders/",
    "T850/Assets/Scenes/",
    "T850/Assets/Layouts/",
    "T850/Assets/Fonts/",
    "T850/Librerias/",       # minus vcpkg (see EXCLUDES)
    "T850/cmake/",
)
# Subtrees synced by file type.
FILETYPE_SUBTREES = ("T850/Assets/", "T850/", "T850/Resources/")
SYNC_EXTENSIONS = (
    ".sln", ".props", ".targets", ".vcxproj", ".filters", ".vcxproj.user",
    ".cmake", ".txt", ".json", ".in", ".bat", ".cmd", ".ps1",
)
EXCLUDES = (
    "T850/Librerias/vcpkg",          # a gitlink; the host has a real checkout
    "T850/T850Launcher.exe",         # tracked build output
)
SUBMODULE_LINKS = ("T850/Librerias/vcpkg",)


def die(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(2)


def git(*args, cwd=REPO):
    out = subprocess.run(["git", *args], cwd=str(cwd), stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, stdin=subprocess.DEVNULL, timeout=600)
    if out.returncode != 0:
        die(f"git {' '.join(args)} failed: {out.stderr.decode(errors='replace').strip()}")
    return out.stdout


def sync_files(source="HEAD"):
    """Repo-relative paths to sync for revision `source`."""
    names = git("ls-tree", "-r", "--name-only", source).decode().splitlines()
    files, links = [], []
    for name in names:
        if not name.startswith("T850/"):
            continue
        if any(name == ex or name.startswith(ex + "/") for ex in EXCLUDES):
            continue
        if name in SUBMODULE_LINKS:
            links.append(name)
            continue
        keep = name.startswith(FULL_SUBTREES)
        if not keep:
            keep = (name.startswith(FILETYPE_SUBTREES)
                    and name.lower().endswith(SYNC_EXTENSIONS))
        if keep:
            files.append(name)
    return sorted(files), links


class Host:
    def __init__(self, host, root):
        self.host = host
        self.root = root
        self.d = f"{host}:{root}"

    def run(self, argv, timeout, error):
        """Run a local SSH/SCP command; a timeout or a missing binary fails loudly."""
        try:
            return subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  stdin=subprocess.DEVNULL, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            die(f"{error}: no answer within {timeout}s")
        except FileNotFoundError as exc:
            die(f"{error}: {exc}")

    def ssh(self, command, check=True, quiet=False, timeout=SSH_TIMEOUT):
        argv = ["ssh", *SSH_OPTS, self.host, command]
        out = self.run(argv, timeout, "ssh")
        if out.returncode != 0 and check:
            die(f"ssh failed ({out.returncode}): {out.stderr.strip()[-800:]}")
        if not quiet and out.stdout:
            print(out.stdout, end="")
        return out

    def powershell(self, script_path, check=True):
        return self.ssh(f'powershell -NoProfile -ExecutionPolicy Bypass '
                        f'-File "{script_path}"', check=check)

    def copy_to(self, local, remote, timeout=SCP_TIMEOUT):
        argv = ["scp", *SSH_OPTS, str(local), f"{self.host}:{remote.replace(chr(92), '/')}"]
        out = self.run(argv, timeout, "scp upload")
        if out.returncode != 0:
            die(f"scp failed: {out.stderr.strip()[-800:]}")

    def copy_from(self, remote, local, timeout=SCP_TIMEOUT):
        Path(local).parent.mkdir(parents=True, exist_ok=True)
        argv = ["scp", "-r", *SSH_OPTS, f"{self.host}:{remote.replace(chr(92), '/')}",
                str(local)]
        out = self.run(argv, timeout, "scp fetch")
        if out.returncode != 0:
            print(f"WARNING: scp fetch failed: {out.stderr.strip()[-400:]}",
                  file=sys.stderr)
            return False
        return True

    def win(self, relative):
        return self.root.rstrip("\\") + "\\" + relative.replace("/", "\\")

    def ps(self, relative):
        return self.win(relative)


def with_lock(func, args):
    LOCK.parent.mkdir(parents=True, exist_ok=True)
    lock = LOCK.open("a+")
    try:
        import fcntl
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
    except ImportError:
        pass
    try:
        return func(args)
    finally:
        try:
            import fcntl
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
        except ImportError:
            pass


# --------------------------------------------------------------------------- sync

def cmd_sync(args):
    host = Host(args.host, args.root)
    source = args.revision
    if args.tree:
        if not args.revision_is_default:
            die("use either --tree or --revision, not both")
        source = None
    if source is None:
        files, links = tree_files(Path(args.tree))
        ident = "tree:" + Path(args.tree).name
    else:
        files, links = sync_files(source)
        ident = "rev:" + git("rev-parse", "--short", source).decode().strip()

    staging = Path(SCRATCH) / "sync"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    manifest = []
    for name in files:
        src = (Path(args.tree) / name) if args.tree else REPO / name
        if not src.exists():
            print(f"WARNING: listed file missing: {name}", file=sys.stderr)
            continue
        dst = staging / "T850" / name[len("T850/"):]
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        # The archive's own timestamps do not matter: the host sets "now" on every file whose
        # content differs from the copy already there, and leaves the rest alone.
        data = dst.read_bytes()
        manifest.append([name, len(data), hashlib.sha1(data).hexdigest()])
    # The submodule git links are written as plain files so the host can tell them apart.
    for link in links:
        dst = staging / "T850" / link[len("T850/"):]
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes((REPO / link).read_bytes() if (REPO / link).is_file() else b"")
    (staging / "manifest.json").write_text(json.dumps(
        {"ident": ident, "files": manifest, "links": links}, indent=1), encoding="utf-8")

    archive = Path(SCRATCH) / "t850_sync.tar.gz"
    with tarfile.open(archive, "w:gz") as tar:
        tar.add(staging / "T850", arcname="T850")
        tar.add(staging / "manifest.json", arcname="manifest.json")
    size_mb = archive.stat().st_size / 1e6
    print(f"sync {ident}: {len(manifest)} files, {size_mb:.1f} MB archive")

    remote_dir = host.win("__parity\\sync")
    host.ssh(f'cmd /c if not exist "{remote_dir}" mkdir "{remote_dir}"')
    host.copy_to(archive, remote_dir + "\\t850_sync.tar.gz")
    ps = Path(SCRATCH) / "sync_extract.ps1"
    ps.write_text(EXTRACT_SCRIPT, encoding="utf-8")
    host.copy_to(ps, remote_dir + "\\sync_extract.ps1")
    # The extract runs over SSH (session 0) but writes in-process, so the lock covers it.
    out = host.ssh(f'powershell -NoProfile -ExecutionPolicy Bypass -File '
                   f'"{remote_dir}\\sync_extract.ps1" -Root "{args.root}" -Source "{remote_dir}"'
                   f' -Ident "{ident}"' + (" -Prune" if args.prune else ""))
    if "ROOT_FAIL" in out.stdout:
        die(out.stdout.strip())
    if "SYNC_OK" not in out.stdout:
        die("extract did not report SYNC_OK")
    host.ssh(f'cmd /c del /q "{remote_dir}\\t850_sync.tar.gz"', check=False)


def tree_files(tree):
    """Tracked and untracked-unignored files of a working tree, repo-relative."""
    names = git("ls-files", "--cached", "--others", "--exclude-standard", cwd=tree).decode()
    files, links = [], []
    for name in names.splitlines():
        full = f"T850/{name}" if not name.startswith("T850/") else name
        if not full.startswith("T850/"):
            continue
        if any(full == ex or full.startswith(ex + "/") for ex in EXCLUDES):
            continue
        if full in SUBMODULE_LINKS:
            links.append(full)
            continue
        keep = full.startswith(FULL_SUBTREES) or (
            full.startswith(FILETYPE_SUBTREES) and full.lower().endswith(SYNC_EXTENSIONS))
        if keep and (tree / full).is_file():
            files.append(full)
    return sorted(set(files)), links


EXTRACT_SCRIPT = r'''param([string]$Root, [string]$Source, [string]$Ident, [switch]$Prune)
$ErrorActionPreference = 'Stop'
# <root> holds start_task.ps1 and __parity; the checkout is <root>\T850 and the project the
# solution and the build scripts use is <root>\T850\T850. Writing into <root>\T850 instead
# lands beside the checkout's .git and builds a copy that has no build\dawn-package.
$dstRoot = Join-Path $Root 'T850\T850'
if (!(Test-Path -LiteralPath (Join-Path $dstRoot 'T850.sln'))) {
    "ROOT_FAIL $dstRoot (no T850.sln; -Root must be the folder that holds T850\T850)"
    exit 1
}
$work = Join-Path $Root '__parity\sync\work'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null
& tar -xzf (Join-Path $Source 't850_sync.tar.gz') -C $work
if ($LASTEXITCODE -ne 0) { "SYNC_FAIL tar exit $LASTEXITCODE"; exit 1 }
$manifest = Get-Content (Join-Path $work 'manifest.json') -Raw | ConvertFrom-Json
$copied = 0; $added = 0; $same = 0
# Changed and new files get the host's current time, so MSBuild's timestamp comparison sees
# them as newer than the objects built from the previous revision. Unchanged files are left
# alone entirely. (A fixed timestamp would hide changes from the incremental build.)
$now = (Get-Date).ToUniversalTime()
foreach ($entry in $manifest.files) {
    $name = $entry[0] -as [string]
    $rel = $name.Substring(5)                      # drop 'T850/'
    $hash = $entry[2] -as [string]
    $src = Join-Path $work ('T850\' + $rel)
    $dst = Join-Path $dstRoot $rel
    if (!(Test-Path $src)) { continue }
    if (Test-Path $dst) {
        $b = (Get-FileHash $dst -Algorithm SHA1).Hash
        if ($b -eq $hash.ToUpper()) {
            # Untouched: leave the time alone, or MSBuild would think it changed.
            $same++
            continue
        }
        Copy-Item -LiteralPath $src -Destination $dst -Force; $copied++
        (Get-Item -LiteralPath $dst).LastWriteTimeUtc = $now
    } else {
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item -LiteralPath $src -Destination $dst -Force; $added++
        (Get-Item -LiteralPath $dst).LastWriteTimeUtc = $now
    }
}
if ($Prune) {
    $keep = @{}
    foreach ($entry in $manifest.files) { $keep[[string]$entry[0]] = $true }
    $existing = & git -C $Root ls-tree -r --name-only HEAD
    $removed = 0
    foreach ($name in $existing) {
        if (!$name.StartsWith('T850/')) { continue }
        if ($keep.ContainsKey($name)) { continue }
        if (!(Test-Path (Join-Path $Root $name))) { continue }
        Remove-Item -LiteralPath (Join-Path $Root $name) -Force; $removed++
    }
    "pruned=$removed"
}
Remove-Item -Recurse -Force $work
"SYNC_OK ident=$Ident files=$($manifest.files.Count) copied=$copied added=$added unchanged=$same"
'''


# --------------------------------------------------------------------------- build

BUILD_SCRIPT = r'''param([string]$Root, [string]$Action = 'Rebuild')
$ErrorActionPreference = 'Continue'
$proj = Join-Path $Root 'T850\T850'
if (!(Test-Path -LiteralPath (Join-Path $proj 'T850.sln'))) {
    "ROOT_FAIL $proj (no T850.sln; -Root must be the folder that holds T850\T850)"
    exit 1
}
$log = Join-Path $Root '__parity\build\build.log'
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
$exe = Join-Path $proj 'bin\x64\Release\DayScene.exe'
$startUtc = (Get-Date).ToUniversalTime()
Set-Location $proj
$sw = [Diagnostics.Stopwatch]::StartNew()
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $proj 'scripts\build.ps1') -Config Release -Platform x64 -Action $Action *>&1 |
    Tee-Object -FilePath $log
$code = $LASTEXITCODE
$sw.Stop()
# The binary must postdate the build start, or nothing was relinked.
$exeTime = (Get-Item -LiteralPath $exe -ErrorAction SilentlyContinue).LastWriteTimeUtc
$newer = $false
if ($exeTime) { $newer = $exeTime -ge $startUtc }
"BUILD_EXIT=$code action=$Action seconds=$([int]$sw.Elapsed.TotalSeconds) startUtc=$($startUtc.ToString('o')) exeUtc=$($exeTime.ToString('o')) exe_newer=$newer"
if ($code -ne 0) {
    Select-String -Path $log -Pattern 'error [A-Z]+[0-9]+|: error ' | Select-Object -First 40 |
        ForEach-Object { "ERR " + $_.Line.Trim() }
}
'''


BUILD_COMMAND = ('powershell -NoProfile -ExecutionPolicy Bypass -File '
                 '"{remote}\\parity_build.ps1" -Root "{root}" -Action "{action}"')


SETUP_DAWN_SCRIPT = r'''param([string]$Root, [string]$Done)
# SetupDawn.ps1 exits the whole script on its own exit calls, so the completion file is
# written by a child process and the exit code comes from it.
$ErrorActionPreference = 'Continue'
$project = Join-Path $Root 'T850\T850'
if (!(Test-Path -LiteralPath (Join-Path $project 'T850.sln'))) {
    "ROOT_FAIL $project (no T850.sln; -Root must be the folder that holds T850\T850)"
    exit 1
}
$log = Join-Path $Root '__parity\build\setup_dawn.log'
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
Remove-Item -LiteralPath $Done -ErrorAction SilentlyContinue
$setup = Join-Path $project 'scripts\SetupDawn.ps1'
$code = 1
try {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    & powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Architecture x64 *>&1 |
        Tee-Object -FilePath $log
    $code = $LASTEXITCODE
    $sw.Stop()
    "SETUP_DAWN_EXIT=$code seconds=$([int]$sw.Elapsed.TotalSeconds)"
} catch {
    $_ | Out-String | Add-Content $log
    "SETUP_DAWN_EXCEPTION $($_.Exception.Message)"
    $code = 1
} finally {
    Set-Content -LiteralPath $Done -Value "exit=$code"
}
if ($code -ne 0) {
    Select-String -Path $log -Pattern 'error|throw|failed|Failed' | Select-Object -Last 15 |
        ForEach-Object { "SETUP " + $_.Line.Trim() }
}
'''


def cmd_build(args):
    host = Host(args.host, args.root)
    remote_dir = host.win("__parity\\build")
    host.ssh(f'cmd /c if not exist "{remote_dir}" mkdir "{remote_dir}"')
    ps = Path(SCRATCH) / "build.ps1"
    ps.write_text(BUILD_SCRIPT, encoding="utf-8")
    host.copy_to(ps, remote_dir + "\\parity_build.ps1")
    # A plain Build only relinks what the timestamps say changed; after a fresh sync the
    # first build must be a full Rebuild (see the sync note: old objects predate the C++).
    action = args.action
    out = host.ssh(BUILD_COMMAND.format(remote=remote_dir, root=args.root, action=action),
                   timeout=BUILD_TIMEOUT)
    text = out.stdout
    if "ROOT_FAIL" in text:
        die(text.strip())
    code = None
    for line in text.splitlines():
        if line.startswith("BUILD_EXIT="):
            code = int(line.split()[0].split("=")[1])
    retried = False
    # SetupDawn is never automatic: it clones a pinned vcpkg and builds Dawn, and Claude asked
    # to be asked first. Only --setup-dawn runs it.
    if code != 0 and args.setup_dawn:
        print("build failed and --setup-dawn was given. Running SetupDawn.ps1 "
              "-Architecture x64 once and retrying")
        setup = Path(SCRATCH) / "setup_dawn.ps1"
        setup.write_text(SETUP_DAWN_SCRIPT, encoding="utf-8")
        host.copy_to(setup, remote_dir + "\\parity_setup_dawn.ps1")
        host.ssh(f'cmd /c if exist "{remote_dir}\\setup_dawn.done" del /q "{remote_dir}\\setup_dawn.done"',
                 check=False)
        host.ssh(f'cmd /c if not exist "{remote_dir}" mkdir "{remote_dir}"')
        out = host.ssh(f'powershell -NoProfile -ExecutionPolicy Bypass -File '
                       f'"{remote_dir}\\parity_setup_dawn.ps1" -Root "{args.root}" '
                       f'-Done "{remote_dir}\\setup_dawn.done"', timeout=BUILD_TIMEOUT)
        print("\n".join(l for l in out.stdout.splitlines()
                        if l.startswith(("SETUP", "Setup", "ERROR", "error"))))
        local_setup = Path("/tmp/winport/build")
        host.copy_from(f"{host.root}/__parity/build/setup_dawn.log",
                       local_setup / "setup_dawn.log")
        out = host.ssh(BUILD_COMMAND.format(remote=remote_dir, root=args.root,
                                            action=action), timeout=BUILD_TIMEOUT)
        if "ROOT_FAIL" in out.stdout:
            die(out.stdout.strip())
        text = out.stdout
        for line in out.stdout.splitlines():
            if line.startswith("BUILD_EXIT="):
                code = int(line.split()[0].split("=")[1])
        retried = True
    local = Path("/tmp/winport/build")
    host.copy_from(f"{host.root}/__parity/build/build.log", local / "build.log")
    summary = [l for l in text.splitlines() if l.startswith("BUILD_EXIT=")]
    print(f"build exit={code} action={action}" + (" (after SetupDawn)" if retried else ""))
    for line in summary:
        print(line)
    if code == 0 and "exe_newer=True" not in summary[0]:
        print("DayScene.exe does not postdate the build start: nothing was relinked",
              file=sys.stderr)
        return 1
    return 0 if code == 0 else 1


# --------------------------------------------------------------------------- run

def remote_task_running(host):
    out = host.ssh('powershell -NoProfile -Command "(Get-ScheduledTask | Where-Object '
                   '{ $_.TaskName -like \'W1*\' -or $_.TaskName -like \'Parity*\' } | '
                   'Where-Object State -eq Running).Count"', quiet=True, check=False)
    try:
        return int(out.stdout.strip().splitlines()[-1]) > 0
    except (ValueError, IndexError):
        return True


def cmd_run(args):
    host = Host(args.host, args.root)
    script = Path(args.script).resolve()
    tag = args.tag
    if not script.is_file():
        die(f"no such script: {script}")
    if remote_task_running(host):
        die("another parity task is running on the host; the lock is held but the task "
            "outlived a previous run. Inspect and stop it first.")
    name = f"W1_{tag}"
    remote_dir = host.win(f"__parity\\{tag}")
    host.ssh(f'cmd /c if exist "{remote_dir}" rmdir /s /q "{remote_dir}"')
    host.ssh(f'cmd /c if not exist "{remote_dir}" mkdir "{remote_dir}"')
    # scp cannot overwrite a file another process has open, so the launcher lives one level up.
    host.copy_to(script, remote_dir + "\\" + script.name)
    # start_task.ps1 passes -File only, so an argument-carrying launcher runs the real script in
    # place: the script stays <root>\__parity\<tag>\<name>.ps1 and the launcher holds the args.
    # Scripts that declare -Exe/-Out get the project's DayScene and <root>\__parity\<tag>\out; no
    # host path is stored in the repository itself.
    text = script.read_text(encoding="utf-8", errors="replace")
    extra = ""
    if "$Exe" in text:
        exe = host.win("T850\\T850\\bin\\x64\\Release\\DayScene.exe")
        extra += f"$Exe = '{exe}'\n"
    if "$Out" in text:
        extra += f"$Out = '{remote_dir}\\out'\n"
    launch = Path(SCRATCH) / f"launch_{tag}.ps1"
    launch.write_text(
        f"$Dir = '{remote_dir}'\n$ScriptPath = '{remote_dir}\\{script.name}'\n"
        + extra + WRAPPER_SCRIPT, encoding="utf-8")
    launch_remote = host.win(f"__parity\\launch_{tag}.ps1")
    host.copy_to(launch, launch_remote)
    launcher = host.win("start_task.ps1")
    print(f"starting task {name}")
    out = host.ssh(f'powershell -NoProfile -ExecutionPolicy Bypass -File "{launcher}" '
                   f'-Name "{name}" -Script "{launch_remote}"')
    if "started" not in out.stdout:
        die("start_task.ps1 did not report starting the task")
    done = remote_dir + "\\DONE.txt"
    started = time.time()
    delay = 3
    while True:
        # Wait before the first poll: the started task needs a moment to create the folder.
        time.sleep(2)
        poll = host.ssh(
            'powershell -NoProfile -Command "if (Test-Path -LiteralPath '
            f"'{done}') {{ 'DONE' }} else {{ 'PENDING' }}\"",
            quiet=True, check=False)
        if "DONE" in poll.stdout:
            break
        if time.time() - started > args.timeout:
            host.ssh(f'powershell -NoProfile -Command "Stop-ScheduledTask -TaskName '
                     f'{name} -ErrorAction SilentlyContinue; Get-Process DayScene '
                     f'-ErrorAction SilentlyContinue | Stop-Process -Force"', check=False)
            print(f"TIMEOUT after {args.timeout}s; task stopped", file=sys.stderr)
            break
        time.sleep(delay)
        delay = min(delay * 2, 30)
    local = Path("/tmp/winport") / tag
    if local.exists():
        shutil.rmtree(local)
    host.copy_from(host.win(f"__parity\\{tag}\\*"), local)
    print(f"results in {local}")
    err = local / "DONE.txt"
    if not err.exists():
        print("DONE.txt did not arrive with the results", file=sys.stderr)
        return 1
    err = local / "ERROR.txt"
    if err.exists():
        print("run reported an error:", err.read_text(errors="replace")[-2000:])
        return 1
    return 0


WRAPPER_SCRIPT = r'''# Runs the real script and always leaves DONE.txt in $Dir; a non-zero exit also leaves
# ERROR.txt with the last lines of the output so the run can be told from a clean one.
$log = Join-Path $Dir 'wrapper.log'
$done = Join-Path $Dir 'DONE.txt'
$errorFile = Join-Path $Dir 'ERROR.txt'
Remove-Item -LiteralPath $done, $errorFile -ErrorAction SilentlyContinue
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptPath *>&1 | Tee-Object -FilePath $log
    $code = $LASTEXITCODE
    if ($null -eq $code) { $code = 0 }
} catch {
    $_ | Out-String | Set-Content $errorFile
    $code = 1
}
if ($code -ne 0 -and !(Test-Path -LiteralPath $errorFile)) {
    $tail = @(Get-Content -LiteralPath $log -ErrorAction SilentlyContinue | Select-Object -Last 40)
    ("exit $code" + [Environment]::NewLine + ($tail -join [Environment]::NewLine)) |
        Set-Content -LiteralPath $errorFile
}
"exit=$code"
Set-Content -LiteralPath $done -Value 'DONE'
'''


# --------------------------------------------------------------------------- cli

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=os.environ.get("T850_WIN_HOST"))
    parser.add_argument("--root", default=os.environ.get("T850_WIN_ROOT"))
    subs = parser.add_subparsers(dest="command", required=True)

    sync = subs.add_parser("sync", help="archive the source and extract it into <root>\\T850")
    sync.add_argument("--tree", help="working tree or worktree, including uncommitted changes")
    sync.add_argument("--revision", default="HEAD", help="revision to sync (default HEAD)")
    sync.add_argument("--prune", action="store_true",
                      help="delete tracked files the new revision removed")
    sync.set_defaults(func=cmd_sync)

    build = subs.add_parser("build", help="MSBuild Release x64 over SSH")
    build.add_argument("--action", choices=("Rebuild", "Build"), default="Rebuild",
                       help="build.ps1 action; the default Rebuild is required after a sync "
                            "(old objects would otherwise hide changed C++)")
    build.add_argument("--setup-dawn", action="store_true",
                       help="run SetupDawn.ps1 even when the failure is not Dawn-related")
    build.set_defaults(func=cmd_build)

    run = subs.add_parser("run", help="run a PowerShell script in session 1 and fetch results")
    run.add_argument("script")
    run.add_argument("tag")
    run.add_argument("--timeout", type=int, default=6 * 3600)
    run.set_defaults(func=cmd_run)

    args = parser.parse_args()
    if not args.host or not args.root:
        die("set --host/--root or T850_WIN_HOST/T850_WIN_ROOT")
    args.revision_is_default = False
    if args.command == "sync":
        # argparse cannot tell whether --revision was given; re-scan the argv.
        args.revision_is_default = "--revision" not in sys.argv
    sys.exit(with_lock(args.func, args) or 0)


if __name__ == "__main__":
    main()
