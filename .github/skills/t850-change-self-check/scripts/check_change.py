#!/usr/bin/env python3
"""Pre-commit checks for a T850 change (skill: .github/skills/t850-change-self-check).

Checks the commits since the merge base with --base, plus staged, unstaged and untracked files.
Read-only: it never builds, runs DayScene or changes the repository.

  python3 .github/skills/t850-change-self-check/scripts/check_change.py [--base origin/master]
          [--desktop-only <path> ...] [--skip-validators]

Exit status: 0 when no check fails (warnings allowed), 1 when a check fails, 2 on a usage error.
"""
import argparse
import json
import os
import py_compile
import re
import subprocess
import sys
import tempfile
from pathlib import Path

PROTECTED = [
    (re.compile(r"^T850/config\.json$"), "machine-local runtime config"),
    (re.compile(r"^T850/Librerias/vcpkg$"), "vcpkg submodule pointer (Dawn and every package are pinned to it)"),
    (re.compile(r"^T850/Assets/\.t850-cloud-assets\.json$"), "local asset-download cache"),
    (re.compile(r"^T850/bin/"), "build output and the shared runtime asset links"),
    (re.compile(r"(^|/)dumps_[^/]*/"), "frame dumps"),
    (re.compile(r"(^|/)VisualBaselines/"), "generated visual baselines"),
]
SCRATCH = re.compile(r"\.(orig|rej|bak)$|(^|/)_probe|(^|/)__parity/|(^|/)dumps_")
KERNEL = re.compile(r"^T850/Assets/Shaders/CS_[^/]+\.hlsl$")
TWIN_INPUTS = re.compile(r"^T850/Assets/Shaders/(CS_[^/]+\.(hlsl|glsl)|compute_kernels\.json)$")
FLUID_INPUTS = re.compile(r"^T850/(Assets/Shaders/(CS_(Fluid|Particle|Pbf)[^/]*\.hlsl|compute_kernels\.json)"
                          r"|Framework/src/utils/ComputeKernelRegistry\.cpp)$")
REFERENCES = re.compile(r"^T850/scripts/validation/backyard8_references\.json$")
# Hashed by SetupDawn.ps1 ($recipeFiles); Windows x64/ARM64 builds fail its check until setup reruns.
DAWN_RECIPE = re.compile(r"^T850/(cmake/(vcpkg-overlays|dawn-package)/|scripts/SetupDawn\.ps1$"
                         r"|Framework/(include|src)/(video/webgpu/WebGPUShaderCompiler|utils/ShaderPreprocessor)\.(h|cpp)$"
                         r"|Librerias/simplecpp/simplecpp\.(h|cpp)$)")
NOT_PORTABLE = [
    (re.compile(r"\b(isfinite|isnan|isinf)\s*\("),
     "WebGPU's Tint aborts on OpIsNan/OpIsInf; test the exponent bits: (asuint(x) & 0x7F800000u) != 0x7F800000u"),
    (re.compile(r"\.(Sample|SampleBias|SampleCmp)\s*\("),
     "implicit-derivative sampling is not valid in a compute shader; use SampleLevel or SampleCmpLevelZero"),
    (re.compile(r"\bdd[xy](_coarse|_fine)?\s*\("), "derivatives are not available in a compute shader"),
]
IPV4 = re.compile(r"(?<![\d.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?![\d.])")
NON_WINDOWS = re.compile(r"STEAM_DECK|LINUX|UNIX|ANDROID|APPLE|NOT\s+(WIN32|MSVC)", re.I)
ANDROID_HINT = (" (Android builds from its own list: add it there, or pass --desktop-only <path> "
                "and say why in the commit message)")

LISTS = {
    "fw_vcx": "T850/Framework/Framework.vcxproj",
    "fw_fil": "T850/Framework/Framework.vcxproj.filters",
    "fw_cmake": "T850/Framework/CMakeLists.txt",
    "ds_vcx": "T850/DayScene/DayScene.vcxproj",
    "ds_fil": "T850/DayScene/App.vcxproj.filters",
    "ds_cmake": "T850/DayScene/CMakeLists.txt",
    "ig_vcx": "T850/FrameworkImGui/FrameworkImGui.vcxproj",
    "ig_fil": "T850/FrameworkImGui/FrameworkImGui.vcxproj.filters",
    "ig_cmake": "T850/FrameworkImGui/CMakeLists.txt",
    "ed_vcx": "T850/T8ditor/T8ditor.vcxproj",
    "ed_props": "T850/T8ditor/EditorSources.props",
    "ed_fil": "T850/T8ditor/T8ditorCore.vcxproj.filters",
    "ed_cmake": "T850/T8ditor/CMakeLists.txt",
    "android": "T850/cmake/AndroidBuild.cmake",
}


class Check:
    def __init__(self, name):
        self.name, self.errors, self.warnings, self.skipped = name, [], [], ""


def git(repo, *args, check=True):
    r = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if check and r.returncode:
        print(f"check_change: git {' '.join(args)} failed: {r.stderr.strip()}", file=sys.stderr)
        sys.exit(2)
    return r.stdout


def name_status(repo, *revs):
    rows = []
    for line in git(repo, "diff", "--name-status", "-M", "--no-color", *revs).splitlines():
        parts = line.split("\t")
        kind = parts[0][:1]
        if kind == "R":
            rows += [("D", parts[1]), ("A", parts[2])]
        elif kind == "C":
            rows.append(("A", parts[2]))
        elif len(parts) > 1:
            rows.append((kind, parts[1]))
    return rows


def added_lines(repo, merge_base, untracked):
    """{path: [(line_number, text)]} for lines added since the merge base, uncommitted included."""
    result, path, number = {}, None, 0
    for line in git(repo, "diff", "-U0", "--no-color", "--no-ext-diff", merge_base).splitlines():
        if line.startswith("+++ "):
            target = line[4:].strip('"')
            path = target[2:] if target.startswith("b/") else None
        elif line.startswith("@@"):
            m = re.search(r"\+(\d+)", line)
            number = int(m.group(1)) if m else 0
        elif line.startswith("+") and path:
            result.setdefault(path, []).append((number, line[1:]))
            number += 1
    for p in untracked:
        try:
            data = (repo / p).read_bytes()
        except OSError:
            continue
        if len(data) > 2_000_000 or b"\0" in data[:8192]:
            continue
        result[p] = list(enumerate(data.decode("utf-8", "replace").splitlines(), 1))
    return result


def has_entry(text, entry):
    return re.search(r"(?<![A-Za-z0-9_.])" + re.escape(entry) + r"(?![A-Za-z0-9_])", text) is not None


def cmake_conditions(text, entry):
    """The if() conditions around `entry` in a CMake file: [] if unconditional, None if absent."""
    stack = []
    pattern = re.compile(r"(?<![A-Za-z0-9_.])" + re.escape(entry) + r"(?![A-Za-z0-9_])")
    for line in text.splitlines():
        s = line.split("#", 1)[0].strip()
        m = re.match(r"(if|elseif|else|endif)\s*\((.*)\)$", s, re.I)
        if m:
            kind = m.group(1).lower()
            if kind == "if":
                stack.append(m.group(2).strip())
            elif kind == "elseif" and stack:
                stack[-1] = m.group(2).strip()
            elif kind == "else" and stack:
                stack[-1] = f"NOT ({stack[-1]})"
            elif kind == "endif" and stack:
                stack.pop()
            continue
        if pattern.search(s):
            return list(stack)
    return None


def build_entries(path):
    """(group, CMake entry, MSBuild entry, Android entry) for a source, or None outside the known roots."""
    name = path.rsplit("/", 1)[-1]
    if path.startswith("T850/Framework/src/"):
        rel = path[len("T850/Framework/"):]
        return "fw", rel, rel.replace("/", "\\"), "Framework/" + rel
    if re.match(r"^T850/DayScene/[^/]+\.cpp$", path):
        return "ds", name, name, "DayScene/" + name
    if path.startswith("T850/FrameworkImGui/src/"):
        rel = path[len("T850/FrameworkImGui/"):]
        return "ig", rel, rel.replace("/", "\\"), "FrameworkImGui/" + rel
    if re.match(r"^T850/T8ditor/[^/]+\.cpp$", path):
        return "ed", name, name, None
    return None


def check_build_lists(repo, added, deleted, desktop_only, check):
    texts = {k: (repo / v).read_text(encoding="utf-8", errors="replace") if (repo / v).exists() else ""
             for k, v in LISTS.items()}

    def need(key, entry, path, hint=""):
        if not has_entry(texts[key], entry):
            check.errors.append(f"{path}: missing from {LISTS[key][5:]} (entry '{entry}'){hint}")

    for path in sorted(p for p in added if p.endswith(".cpp")):
        where = build_entries(path)
        if where is None:
            if path.startswith("T850/") and not path.startswith("T850/Librerias/"):
                check.warnings.append(f"{path}: not under a known build-list root; confirm its build lists by hand")
            continue
        group, cmake_entry, msbuild_entry, android_entry = where
        if "android" in path.lower():
            if android_entry:
                need("android", android_entry, path)
            continue
        if group == "ed":
            need("ed_cmake", cmake_entry, path)
            if cmake_entry == "EditorMain.cpp":
                need("ed_vcx", msbuild_entry, path)
            else:
                need("ed_props", msbuild_entry, path)
                need("ed_fil", msbuild_entry, path)
            continue
        need(f"{group}_cmake", cmake_entry, path)
        conditions = cmake_conditions(texts[f"{group}_cmake"], cmake_entry) or []
        if not any(NON_WINDOWS.search(c) for c in conditions):
            need(f"{group}_vcx", msbuild_entry, path)
            # DayScene has no live filters file: App.vcxproj.filters matches no project name.
            if group != "ds":
                need(f"{group}_fil", msbuild_entry, path)
        if not conditions and path not in desktop_only:
            need("android", android_entry, path, ANDROID_HINT)

    for path in sorted(p for p in deleted if p.endswith(".cpp")):
        where = build_entries(path)
        if where is None:
            continue
        group, cmake_entry, msbuild_entry, android_entry = where
        for key in (k for k in LISTS if k.startswith(group + "_")):
            entry = cmake_entry if key.endswith("_cmake") else msbuild_entry
            if has_entry(texts[key], entry):
                check.errors.append(f"{path}: deleted but still listed in {LISTS[key][5:]} ('{entry}')")
        if android_entry and has_entry(texts["android"], android_entry):
            check.errors.append(f"{path}: deleted but still listed in cmake/AndroidBuild.cmake")


def run_validator(repo, script, check):
    path = repo / "T850" / "scripts" / "validation" / script
    if not path.exists():
        check.warnings.append(f"{script} not found")
        return
    r = subprocess.run([sys.executable, str(path)], cwd=repo / "T850", capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=900)
    if r.returncode:
        tail = (r.stdout + r.stderr).strip().splitlines()[-8:]
        check.errors.append(f"{script} exited {r.returncode}:\n" + "\n".join("        " + t for t in tail))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--base", help="base ref (default origin/master, else master)")
    ap.add_argument("--desktop-only", action="append", default=[], metavar="PATH",
                    help="a new source that must not build on Android (repeatable)")
    ap.add_argument("--skip-validators", action="store_true",
                    help="skip check_gl_twins.py and check_fluid_constants.py")
    args = ap.parse_args()

    top = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    if top.returncode:
        print("check_change: run inside the T850 repository", file=sys.stderr)
        return 2
    repo = Path(top.stdout.strip())
    candidates = [args.base] if args.base else ["origin/master", "master"]
    base = next((b for b in candidates
                 if git(repo, "rev-parse", "--verify", "--quiet", b, check=False).strip()), None)
    if base is None:
        print(f"check_change: base {' or '.join(candidates)} not found", file=sys.stderr)
        return 2
    merge_base = git(repo, "merge-base", base, "HEAD").strip()
    desktop_only = {p if p.startswith("T850/") else "T850/" + p.lstrip("./") for p in args.desktop_only}

    committed = {p for _, p in name_status(repo, merge_base, "HEAD")}
    staged = set(git(repo, "diff", "--cached", "--name-only").splitlines())
    untracked = [p for p in git(repo, "ls-files", "--others", "--exclude-standard").splitlines() if p]
    net = name_status(repo, merge_base)
    added = {p for k, p in net if k == "A"} | set(untracked)
    deleted = {p for k, p in net if k == "D"}
    changed = {p for _, p in net} | set(untracked)
    worktree_only = ({p for _, p in name_status(repo, "HEAD")} | set(untracked)) - committed - staged
    lines = added_lines(repo, merge_base, untracked)
    commits = []
    for record in git(repo, "log", "--format=%h%x1f%an%x1f%ae%x1f%cn%x1f%ce%x1f%s%x1f%B%x1e",
                      f"{merge_base}..HEAD").split("\x1e"):
        record = record.strip("\n")
        if record:
            commits.append(record.split("\x1f", 6))

    print("T850 change self-check")
    print(f"  base {base}, merge base {merge_base[:7]}, {len(commits)} commits, {len(changed)} changed files "
          f"({len(untracked)} untracked)")
    checks = []

    c = Check("protected-files"); checks.append(c)
    for path in sorted(committed | staged | worktree_only):
        for pattern, why in PROTECTED:
            if pattern.search(path):
                if path in committed or path in staged:
                    c.errors.append(f"{path}: {why}; do not commit it")
                else:
                    c.warnings.append(f"{path}: {why}; changed locally, keep it out of commits")
                break

    c = Check("attribution"); checks.append(c)
    for short, an, ae, cn, ce, subject, body in commits:
        if re.search(r"(?im)^\s*co-authored-by:.*copilot", body):
            c.errors.append(f"{short} '{subject}': Co-authored-by Copilot trailer "
                            "(repository rule: no Copilot attribution)")
        if "copilot" in f"{an} {ae} {cn} {ce}".lower():
            c.errors.append(f"{short} '{subject}': author or committer is Copilot")

    c = Check("host-details"); checks.append(c)
    secrets = []
    for var in ("T850_WIN_HOST", "T850_WIN_ROOT"):
        value = os.environ.get(var, "").strip()
        if len(value) >= 4:
            secrets.append((var, value.lower()))
            if "@" in value and len(value.split("@", 1)[1]) >= 4:
                secrets.append((var + " host", value.split("@", 1)[1].lower()))
    for path, rows in sorted(lines.items()):
        if path.startswith("T850/Librerias/"):
            continue
        for number, text in rows:
            for m in IPV4.finditer(text):
                a, b, d3, d4 = (int(x) for x in m.groups())
                if max(a, b, d3, d4) <= 255 and (a == 10 or (a == 172 and 16 <= b <= 31) or (a == 192 and b == 168)):
                    c.errors.append(f"{path}:{number}: private IP address {m.group(0)}")
            low = text.lower()
            for var, value in secrets:
                if value in low:
                    c.errors.append(f"{path}:{number}: contains the value of {var}")

    c = Check("hlsl-portability"); checks.append(c)
    for path, rows in sorted(lines.items()):
        if not KERNEL.match(path):
            continue
        for number, text in rows:
            code = text.split("//", 1)[0]
            for pattern, why in NOT_PORTABLE:
                m = pattern.search(code)
                if m:
                    c.errors.append(f"{path}:{number}: '{m.group(0).strip()}': {why}")

    for name, pattern, script in (("gl-twins", TWIN_INPUTS, "check_gl_twins.py"),
                                  ("fluid-constants", FLUID_INPUTS, "check_fluid_constants.py")):
        c = Check(name); checks.append(c)
        if args.skip_validators:
            c.skipped = "--skip-validators"
        elif not any(pattern.match(p) for p in changed):
            c.skipped = "no relevant change"
        else:
            run_validator(repo, script, c)

    c = Check("build-lists"); checks.append(c)
    check_build_lists(repo, added, deleted, desktop_only, c)

    c = Check("json"); checks.append(c)
    for path in sorted(p for p in changed if p.endswith(".json") and (repo / p).is_file()
                       and not p.startswith(("T850/Librerias/", "T850/bin/"))):
        try:
            json.loads((repo / path).read_text(encoding="utf-8-sig"))
        except (ValueError, UnicodeDecodeError) as e:
            c.errors.append(f"{path}: {e}")

    c = Check("python"); checks.append(c)
    with tempfile.TemporaryDirectory() as scratch:
        for path in sorted(p for p in changed if p.endswith(".py") and (repo / p).is_file()):
            try:
                py_compile.compile(str(repo / path), cfile=os.path.join(scratch, "x.pyc"), doraise=True)
            except py_compile.PyCompileError as e:
                c.errors.append(f"{path}: {e.msg.strip().splitlines()[-1]}")

    c = Check("whitespace"); checks.append(c)
    # cr-at-eol: the generated GL twins are CRLF on purpose, so a CR before the newline is not trailing space.
    r = subprocess.run(["git", "-C", str(repo), "-c", "core.whitespace=blank-at-eol,space-before-tab,blank-at-eof,cr-at-eol",
                        "diff", "--check", merge_base], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode:
        for line in r.stdout.splitlines():
            if not line or line.startswith("+"):
                continue
            # Generated files (GL twins carry a "GENERATED from" banner) keep their generator's layout.
            try:
                head = (repo / line.split(":", 1)[0]).read_bytes()[:512]
            except OSError:
                head = b""
            if b"GENERATED from" not in head and len(c.errors) < 10:
                c.errors.append(line)

    c = Check("review-flags"); checks.append(c)
    for path in sorted(changed):
        if REFERENCES.match(path):
            c.warnings.append(f"{path}: reference re-freeze; needs reviewer sign-off and visual evidence")
        if DAWN_RECIPE.match(path):
            c.warnings.append(f"{path}: Dawn recipe file; Windows x64/ARM64 builds report 'Dawn setup changed' "
                              "until SetupDawn.ps1 reruns on each machine (say so in the commit message)")
    for path in sorted(untracked):
        if SCRATCH.search(path):
            c.warnings.append(f"{path}: scratch file left in the tree")

    failed = [c for c in checks if c.errors]
    warned = [c for c in checks if c.warnings]
    for c in checks:
        status = "FAIL" if c.errors else "WARN" if c.warnings else "SKIP" if c.skipped else "PASS"
        print(f"{status:5s} {c.name}" + (f" ({c.skipped})" if status == "SKIP" else ""))
        for e in c.errors:
            print(f"      error: {e}")
        for w in c.warnings:
            print(f"      warning: {w}")
    print(f"RESULT: {'FAIL' if failed else 'PASS'} ({len(failed)} failed, {len(warned)} with warnings)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
