# Pico2Seq agent rules

Follow `CLAUDE.md` for the firmware architecture, build commands, and testing policy.

## Automatic branch publishing

**Scope: Delta threads only.** The rules in this section apply only to agents
running inside Delta (an Isolated Delta Worktree that has a `local` remote and
the `land` skill). Any other agent, including Claude Code cloud sessions and
Codex, follows its own session instructions for branches, pushes and pull
requests, and these rules do not apply to it.

The user authorizes the following workflow for this repository. It replaces the
personal "never commit, stage, push, or create a branch unless asked" rule for
completed implementation tasks only.

- Before an implementation task, record `git --no-optional-locks status` and
  identify changes that already exist. Do not claim or automatically commit them.
- If a Land child has published this thread's work, synchronize that exact
  published commit using the skill's handoff procedure before editing again.
- Before making changes in a top-level thread, allocate that thread's own branch
  using the branch-ownership procedure in `.delta/skills/land/SKILL.md`.
  Each new top-level thread gets a new branch, even if it starts on another
  thread's branch. Subsequent tasks in the same thread reuse its branch.
- After completing and validating an implementation task, follow
  `.delta/skills/land/SKILL.md` to commit the task's changes and push the branch
  to `local` and to the verified GitHub source remote. This is standing permission:
  do not ask again about staging known task files, committing, or normal pushes.
- Do not publish from reviews, ordinary subthreads, or attached subagents.
  Their changes must return to the top-level thread first. A **Land Changes**
  subthread is the exception: it may publish the parent's branch as that skill
  describes.
- Do not publish interrupted tasks, work with failed required checks (below),
  unrelated edits, secrets, generated build output, or dirty submodule contents.
- A discussion, investigation, or plan does not request publication. Installing
  this publishing workflow does not itself publish existing changes.
  **Land Changes** or an explicit publish request can publish the workflow setup.
- Never merge into the default branch, push to `main`, `master`, or `DeCluttered`,
  force-push, rewrite published history, or delete branches. Local threads do not
  open PRs: the user creates and merges them.
- **Cloud sessions** (no `local` remote; the session assigns the branch, e.g.
  `ccr-*`) differ from local threads: they commit on the assigned branch, push to
  `origin` only, and open a **draft** PR because their session rules require one.
  They never mark it ready or merge it. The `delta/*` naming, the `local` remote
  and the branch-allocation steps in the land skill apply to local threads only.
- On authentication, network, local-remote, or non-fast-forward failures, retain
  the work and report the exact blocker. Do not loop, change destinations,
  disable hooks, or weaken safety checks to make the push pass.
- Keep a successful publication report short: commit, published branch and the
  checks actually run, followed by the ready-to-paste PR description the land skill
  specifies (Changes / Verification / Not verified). Report blockers prominently.

## Required checks

"Validated" means the checks for the files changed (step 2 of
`.delta/skills/land/SKILL.md` has the full table). In short:

- **Host-testable code or tests:** `cmake -P scripts/check_host_tests.cmake` (any
  OS). About thirty host tests already fail on a clean checkout, so the gate
  compares failing test *names* with `tests/known_failures.txt`: a new failure, a
  listed test that now passes, or a build failure blocks. Never add a name to that
  list to make a push pass.
- **Firmware-only code** (OLED, LEDMatrix, `Matrix.cpp`, the Wire-bound UI and app
  files, `Pico2Seq.ino`): compile with `arduino-cli` (command in `CLAUDE.md`). If
  the toolchain is unavailable, report "firmware not compiled"; never call it passing.
- **Docs or config only:** `git diff --check`.

This repository has no CI workflows, so these local checks are the only automated
gate. Do not describe them as CI.

This is agent-driven publishing at task completion, not a filesystem watcher:
manual edits while the agent is idle are not automatically committed.
