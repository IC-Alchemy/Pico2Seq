---
name: land
description: >-
  Commit and publish Pico2Seq work to a separate GitHub branch for this thread.
  Use for Land Changes, explicit publishing, and automatic publication after
  completed implementation tasks. Never create a PR or merge into the default branch.
metadata:
  delta-action: land
---

# Publish a thread branch

The user has authorized ordinary commits and branch pushes without repeated
confirmation. Publish to the user's `local` repository and the verified GitHub
source remote. Leave PR creation and merging to the user.

## 1. Inspect and check ownership

Read the applicable project rules, then inspect:

```sh
git --no-optional-locks status --short --branch
git --no-optional-locks diff
git --no-optional-locks diff --cached
git --no-optional-locks remote -v
git branch --show-current
git config --local --get delta.publishBranch
```

A missing `delta.publishBranch` is expected on a fresh checkout. This is a
repository-local Git config key owned by this workflow, not a Delta app setting.
Do not put this value in a tracked file, global Git config, or `.agents/prepare`.
The authoritative ownership record is the allocation message in this top-level
thread, including the branch and starting commit. The local key is only a cache:
neither an inherited key nor a `delta/*` prefix proves thread ownership.

In a **top-level thread**:

- First check whether this conversation already allocated a branch for this
  thread, not a branch mentioned in copied context from another thread or fork.
  If so, use that exact branch and restore the local config key if this checkout
  was recreated on another machine. Missing history, conflicting config, or
  unclear ownership is a blocker: do not silently allocate a replacement.
- For a new top-level thread without its own allocation record, choose
  `delta/<short-task-slug>-<UTC-timestamp>-<random-suffix>`.
  Verify the name is valid and absent from both the local branches and the
  publication remotes, then run `git switch -c <branch>` from the thread's
  current HEAD and `git config --local delta.publishBranch <branch>`.
  Use a unique suffix; do not reuse an inherited `delta/*` branch just because
  the checkout started on it, even if an inherited local config key names it.
  Record the allocated branch and starting commit in this conversation once so
  later turns on another machine can recover the same ownership.
- Once allocated, HEAD must be on that branch. Stop on a mismatch rather than
  silently moving work.
- New managed checkouts have their own local config. If the user has adopted a
  shared existing checkout instead, ask them to use **Isolated Delta Worktree**
  for new threads: a shared checkout cannot provide independent per-thread
  branch ownership safely.

In a **Land Changes subthread**:

- Require the parent's explicit allocation record and verify that HEAD is on
  exactly that branch. An inherited `delta/*` name alone is not evidence.
  Do not allocate a second branch for the Land child.
- If ownership is missing or ambiguous, stop and ask for the parent thread to
  run the branch-ownership step above, then retry Land.
- Do not run while the parent is still editing or publishing the same work.

Never create or publish from reviews, ordinary subthreads, or attached subagents.
Never push a branch outside `delta/*`, including the remote's default branch.

## Land-to-parent handoff

Before the parent edits again, fetch the published branch and verify the exact
commit reported by the Land child. The parent branch's HEAD must be an ancestor
of that commit. Do not wait for a later push rejection to discover divergence.

- If the parent is clean, use `GIT_EDITOR=true git merge --ff-only <commit>`.
- If the parent's pending files are the snapshot just published by Land, first
  verify this with a temporary Git index: `read-tree HEAD`, `git add -- <owned
  paths>`, and `write-tree`, using `GIT_INDEX_FILE` pointing at a nonexistent file
  in the terminal scratch directory. The resulting tree must equal
  `git rev-parse <commit>^{tree}`. Do not change the real index during this check.
  Also require no unrelated pending files or pre-staged work.
  Only after confirming tree equality and fast-forward ancestry, run
  `git reset --mixed <commit>` to adopt the published history and index while
  preserving every working file. This is a forward-only history handoff, not
  permission to reset to an older or divergent commit. Never use `--hard`.
- If either condition cannot be established, preserve the files and index and
  report the blocker. Do not stash, discard changes, or rewrite history.

## 2. Select and validate the work

- For automatic task completion, include only changes belonging to the completed
  task, comparing with the status recorded before implementation.
- For an explicit **Land Changes** request, include the parent's intended pending
  work. Inspect untracked files and staged files too; ask only when ownership or
  intended inclusion is genuinely ambiguous.
- Stage explicit file paths, not a blanket `git add -A`. Do not stage unrelated
  changes, `.env` files, keys, credentials, build products, or uncommitted changes
  inside submodules. Stop if pre-staged unrelated work would enter the commit.
- Run the checks required for the files changed by the task. Reuse successful
  checks from this task only if the contents have not changed since they ran.
  For documentation/configuration-only changes, validate their syntax and run
  `git diff --check`; a firmware build is not required.
- Failed required checks block the commit and push. Report environment blockers
  honestly; do not label unrun tests as passed.
- Re-check `git --no-optional-locks status` immediately before staging.
  Inspect the staged diff and run `git diff --cached --check` before committing.
- Inspect outgoing commits and their diffs against each destination as well.
  A clean tree alone does not prove that existing unpublished commits belong to
  this task. For a new remote branch, review history since the recorded starting
  commit and verify that the starting commit already exists on the source remote.
  Routine retries may publish the already-reviewed task commit; stop if earlier
  unpublished history, secrets, ownership, or validation is ambiguous.

## 3. Commit once, push both destinations

Verify the remotes on the live checkout. `local` is the user's repository,
not GitHub. The source remote must point to `IC-Alchemy/Pico2Seq` on GitHub;
it is currently `origin`. Stop if the destination has changed or is ambiguous.
Do not add or replace remotes automatically.

Use an imperative commit subject describing the work, then:

```sh
GIT_EDITOR=true git commit -m "<subject>"
git push local "<branch>:refs/heads/<branch>"
git push --set-upstream "<source-remote>" "<branch>:refs/heads/<branch>"
```

Run the pushes in order and stop at the first failure. Keep the commit so a retry
can push it without creating another commit. Do not require a new empty commit
when the tree is already clean: push existing unpublished commits instead.
If both remote branches already equal HEAD, report "already published" and stop.

Use ordinary fast-forward pushes only. Never force-push, amend published commits,
rebase published history, delete remote branches, create PRs, or merge to the
default branch. A rejection is a blocker, not permission to overwrite.

## 4. Verify and report

Verify both destinations explicitly:

```sh
git rev-parse HEAD
git ls-remote local "refs/heads/<branch>"
git ls-remote "<source-remote>" "refs/heads/<branch>"
git --no-optional-locks status --short --branch
```

Both remote branch hashes must equal HEAD before claiming publication.
Report the commit, branch, checks actually run, and any excluded changes or
partial push. Say "published for your PR", never "merged into main".
Do not claim CI passed unless its result was actually observed.

For a Land child, use `report-landed` for the result card, with the thread branch
as the target and CI marked unverified if not checked. Send the parent a handoff
message containing the exact branch, commit, and checks, and direct it to run
the handoff above before any further edits. The card itself does not synchronize
the parent's Git history. Never create a duplicate branch or force-push.
