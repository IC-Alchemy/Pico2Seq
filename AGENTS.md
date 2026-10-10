# Pico2Seq agent rules

Follow `CLAUDE.md` for the firmware architecture, build commands, and testing policy.

## Automatic branch publishing

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
- Do not publish interrupted tasks, work with failed required checks, unrelated
  edits, secrets, generated build output, or dirty submodule contents.
- A discussion, investigation, or plan does not request publication. Installing
  this publishing workflow does not itself publish existing changes.
  **Land Changes** or an explicit publish request can publish the workflow setup.
- Never open a PR, merge into the default branch, push to `main`, `master`, or
  `DeCluttered`, force-push, rewrite published history, or delete branches.
  The user creates PRs and merges them.
- On authentication, network, local-remote, or non-fast-forward failures, retain
  the work and report the exact blocker. Do not loop, change destinations,
  disable hooks, or weaken safety checks to make the push pass.
- Keep successful publication reporting to one line: commit, published branch,
  and checks actually run. Report blockers prominently.

This is agent-driven publishing at task completion, not a filesystem watcher:
manual edits while the agent is idle are not automatically committed.
