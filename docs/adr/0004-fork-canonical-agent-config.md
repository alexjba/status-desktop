# ADR 0004: Agent config is canonical in the fork, symlinked into the upstream clone

## Status

Accepted

## Context

Two long-lived checkouts of the same codebase exist on this machine:

- `~/Repos/alexjba/status-desktop` — personal fork (`alexjba/status-desktop`), used by a fleet of autonomous agents working in parallel worktrees. All its PRs are fork-internal; changes are upstreamed to `status-im` manually by the human.
- `~/Repos/status-desktop` — clone of `status-im/status-desktop`, used for upstream work. Agent config files there (`CLAUDE.md`, `CONTEXT.md`, `.claude/settings.json`) could not be committed without polluting upstream PRs, so they lived as untracked files — and therefore **never propagated into git worktrees**, leaving worktree agents without project context.

Both checkouts should share one config: docs, glossary, ADRs, permissions, guardrail hooks.

## Decision

The fork's `master` is the single source of truth. `CLAUDE.md`, `AGENTS.md` (symlink to `CLAUDE.md`), `CONTEXT.md`, `docs/adr/`, `docs/AGENT-WORKFLOW.md`, and `.claude/settings.json` are **committed** to the fork. This is safe because the fork never PRs upstream (fork-internal PRs only).

The upstream clone replaces its untracked copies with **absolute symlinks** into the fork. A user-level `SessionStart` hook re-creates missing symlinks in any status-desktop checkout that lacks them (covers upstream worktrees, which don't inherit untracked files).

Shared content must be written **remote-aware** where the two repos genuinely differ (e.g. the Branch Policy section keys off `git remote get-url origin`).

Per-machine/per-session state stays out: `.claude/settings.local.json` and `.claude/worktrees/` are gitignored.

## Consequences

- Fork worktrees get full config automatically via git; upstream worktrees get it via the hook.
- Editing config in the upstream clone edits the fork's working tree through the symlink — commits of config happen only in the fork.
- The fork's `master` permanently diverges from upstream by these config commits; manual upstream syncs must preserve them (merge, or rebase them on top). Never hard-reset `master` to upstream.
- Branches cut from fork `master` carry the config commits in history. Manual upstreaming extracts a clean branch with `git rebase --onto upstream/master origin/master <branch>` (recipe in `docs/AGENT-WORKFLOW.md`).
- If the fork checkout moves or is deleted, the upstream clone's symlinks dangle and the hook must be updated.

## Alternatives considered

- **Third shared config repo, symlinked from both** — cleaner separation, but the fork's worktrees would then also need hook-created symlinks, losing the main win (committed files propagate to worktrees for free).
- **Copy + sync script** — no symlink quirks, guaranteed drift the day the script isn't run.
