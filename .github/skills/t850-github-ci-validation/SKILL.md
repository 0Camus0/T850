---
name: t850-github-ci-validation
description: "Use when asked to ensure a T850 pull request or pushed branch is green in GitHub Actions; monitor Windows, Android, Steam Deck, and registration jobs; verify checks belong to the current head SHA; diagnose failed jobs; rerun infrastructure failures; or report PR build health."
argument-hint: "State PR number or branch/SHA, whether pushing is authorized, and whether to monitor only or diagnose/fix failures."
---

# T850 GitHub Actions Build Validation

Use this workflow to prove the current PR head is green. A green run for an older commit is not evidence for the current branch.

## 1. Respect Push Authorization

Monitoring and log inspection are read-only. Fetching is read-only. Do not commit or push unless the user explicitly authorizes it.

Before any authorized push:

```powershell
$RepoRoot = git rev-parse --show-toplevel
Set-Location $RepoRoot
git status --short
git diff --check
git branch --show-current
git remote -v
git config user.name
git config user.email
```

Never stage machine-local changes such as `T850/config.json`, a locally advanced `T850/Librerias/vcpkg`, temporary logs/dumps, or unrelated user edits.

## 2. Verify GitHub CLI Access and PR Identity

```powershell
gh auth status
gh pr view PR_NUMBER --json url,number,title,baseRefName,headRefName,headRefOid,mergeable
```

Require:

- repository is `0Camus0/T850` or the intended fork;
- base branch is the intended target, normally `master`;
- `headRefOid` equals the commit being validated;
- PR is not accidentally closed/merged when validation is expected.

Capture the SHA explicitly:

```powershell
$HeadSha = gh pr view PR_NUMBER --json headRefOid --jq .headRefOid
$LocalSha = git rev-parse HEAD
$RemoteSha = git rev-parse origin/BRANCH
```

If the three SHAs differ, stop and resolve which tree the user wants validated.

## 3. Push Safely When Authorized

Fetch first and verify there is no unexpected divergence:

```powershell
git fetch origin BRANCH
Write-Output "LOCAL=$(git rev-parse HEAD)"
Write-Output "REMOTE=$(git rev-parse origin/BRANCH)"
git log --oneline origin/BRANCH..HEAD
git log --oneline HEAD..origin/BRANCH
```

Use a normal push only:

```powershell
git push origin BRANCH
```

Do not force-push unless explicitly requested and the consequences are understood. After pushing:

```powershell
git fetch origin BRANCH
if ((git rev-parse HEAD) -ne (git rev-parse origin/BRANCH)) {
  throw 'Remote head does not match local head'
}
```

## 4. Find the Workflow Run for the Exact SHA

The PR workflow is `.github/workflows/build.yml` and runs on pull requests targeting `master`.

```powershell
gh run list --commit $HeadSha --limit 5 `
  --json databaseId,workflowName,status,conclusion,headSha,url `
  --jq '.[] | [.databaseId,.workflowName,.status,(.conclusion // "pending"),.headSha,.url] | @tsv'
```

Select the `Build` workflow whose `headSha` exactly matches `$HeadSha`.

If no run appears immediately after push, query the PR rollup:

```powershell
gh pr view PR_NUMBER --json headRefOid,statusCheckRollup
```

Do not monitor a run ID copied from a previous commit.

## 5. Expected PR Jobs

A complete T850 PR run contains:

```text
validate-build-registration
build (Win32, Debug)
build (Win32, Release)
build (x64, Debug)
build (x64, Release)
build (ARM64, Debug)
build (ARM64, Release)
android (arm64-v8a, arm64-v8a, arm64-android)
android (x86_64, x86_64, x64-android)
steamdeck
```

The `release` job is expected to be skipped for ordinary pull requests because it runs only for `v*` tags. A skipped release job is not a PR failure.

Gameplay self-tests are intentionally disabled on GitHub-hosted Windows runners and must be supplied by the local validation workflow.

## 6. Monitor to a Final Result

Interactive option:

```powershell
gh run watch RUN_ID --exit-status --interval 15 --compact
```

Some terminal wrappers cannot capture GitHub CLI alternate-screen output. Use noninteractive status queries instead:

```powershell
gh api repos/0Camus0/T850/actions/runs/RUN_ID `
  --jq '(.status + " " + (.conclusion // "pending"))'

gh run view RUN_ID --json jobs `
  --jq '.jobs[] | [.databaseId,.name,.status,(.conclusion // "pending")] | @tsv'
```

Do not poll continuously or sleep in an agent loop. Use a watcher/background notification when available, then inspect the final result.

Require overall `completed success`, not merely that some jobs are green.

## 7. Diagnose a Failed Job

Resolve the exact failed job ID:

```powershell
gh run view RUN_ID --json jobs `
  --jq '.jobs[] | select(.conclusion == "failure") | [.databaseId,.name] | @tsv'
```

Read only failed-step logs:

```powershell
gh run view --job JOB_ID --log-failed
```

Filter large logs to the first real error and nearby context:

```powershell
gh run view --job JOB_ID --log-failed |
  Select-String -Pattern `
    'error C[0-9]+|fatal error|undefined reference|FAILED:|CMake Error|curl:|Process completed' `
    -Context 5,10
```

Report:

- run attempt and head SHA;
- job and failed step;
- first causal error;
- source file/line when present;
- downstream errors ignored;
- source defect versus infrastructure failure.

Do not diagnose from the final `Process completed with exit code` line alone.

## 8. Source Failure Versus Infrastructure Failure

### Source/product failure

Examples:

- compiler or linker error in project code;
- source-registration mismatch;
- CMake configuration failure caused by repository files;
- missing expected executable/APK/package;
- shader compile failure;
- deterministic test/package validation failure.

Fix locally, run `t850-local-build-validation`, commit/push only when authorized, and monitor the new SHA's new run.

### Infrastructure/transient failure

Examples:

- runner network timeout downloading a dependency;
- GitHub service outage;
- cache service failure;
- external registry endpoint unavailable.

Confirm from the first actual error. One rerun is reasonable:

```powershell
gh run rerun RUN_ID --failed
```

Then verify the attempt number:

```powershell
gh run view RUN_ID --json attempt,status,conclusion
```

If the same external endpoint fails repeatedly, harden the repository download with bounded retries and independent mirrors when appropriate. Validate shell syntax locally, push only when authorized, and require a new green run. Do not repeatedly rerun a deterministic source failure.

Node.js action deprecation annotations are warnings, not failures, unless a step actually exits nonzero.

## 9. Validate the Final Attempt

After completion:

```powershell
gh pr view PR_NUMBER --json url,headRefOid,mergeable,statusCheckRollup
gh run view RUN_ID --json attempt,status,conclusion,jobs
```

Require:

- PR `headRefOid` still equals the validated SHA;
- run `headSha` equals that SHA;
- registration success;
- all six Windows cells success;
- both Android ABIs success;
- Steam Deck success;
- only conditionally irrelevant jobs skipped;
- PR remains mergeable or any conflict is reported separately.

If another commit lands while checks are running, discard the old conclusion and validate the new head.

## 10. Final Report

Report concisely:

- PR URL/number;
- validated head SHA and commit subject;
- workflow run URL/ID and attempt;
- each platform group result;
- failed/rerun history and root cause, if any;
- local tests that GitHub intentionally does not run;
- mergeability;
- remaining warnings or skipped jobs;
- whether any commit/push was performed.

Never say "all builds are healthy" when Steam Deck or an Android ABI is still queued, skipped unexpectedly, blocked, or belongs to an older SHA.

Related skills:

- `t850-local-build-validation`
- `t850-build-run`
- `t850-platform-deploy`
- `t850-crash-debugging`

Relevant repository files:

- `.github/workflows/build.yml`
- `T850/scripts/ValidateBuildRegistration.ps1`
- `T850/scripts/RunWindowsBuildMatrix.ps1`
- `T850/scripts/android/BuildAndroid.bat`
- `T850/steamdeck/BuildSteamRuntime.sh`
