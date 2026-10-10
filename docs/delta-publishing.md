# Delta branch publishing

Pico2Seq's `AGENTS.md` authorizes the agent to commit and publish completed,
validated implementation tasks without asking for routine Git permission.
The workflow is defined once in `.delta/skills/land/SKILL.md`.

## Normal use

1. Start a top-level thread using **Isolated Delta Worktree**.
2. Ask for an implementation change. Before editing, the agent creates a unique
   `delta/<task>-<timestamp>-<suffix>` branch for that thread.
3. When the task and its required checks succeed, the agent commits the task's
   files and pushes that branch to `local` and the verified GitHub source remote.
4. Create a GitHub PR yourself and merge it when you are ready.

Later tasks in the same thread use the same branch. A new top-level thread gets
a new branch. Nothing automatically creates a PR or changes the default branch.
The repository currently uses `DeCluttered` as its remote default branch.

This is an agent instruction, not a Delta background Git service or filesystem
watcher. Manual edits while the agent is idle are not automatically published.
Interrupted tasks and tasks with failed required checks remain uncommitted.
Authentication or push failures are reported without discarding the work.

## Land Changes

**Land Changes** (Alt+L on Windows/Linux; Cmd+L on macOS) runs the same workflow
in a dedicated Land subthread. `/land` runs it directly in the current thread.
The project skill is named `land` so it overrides the existing personal `land`
skill instead of adding a second button choice.

Normally automatic publication has already done the work, and Land verifies
the branch or retries a failed push. Stop parent edits before using a Land
child. A Land child publishes the parent's branch, not a new branch of its own;
the parent agent must verify and adopt its published commit before editing again.
The skill defines a forward-only handoff that preserves working files and
stops if the parent has unrelated or divergent work.

## Activate this setup for future threads

Installing this workflow does not itself publish changes. The setup thread
already has its own publishing branch: choose **Land Changes** to publish the
configuration, or invoke `/land` directly in the top-level setup thread.
Create and merge that PR yourself, then start new isolated threads from the
updated default branch. Until the configuration is in their starting checkout,
other threads do not inherit these rules.

The setup applies to Pico2Seq, not every repository or every existing thread.
It does not change app-wide settings or your personal rules for other projects.

Reference: [Delta: Reviewing & Syncing Changes](https://delta.dev/docs/agents/review-and-sync).
