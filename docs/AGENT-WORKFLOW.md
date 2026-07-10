# Agent Fleet Workflow (alexjba/status-desktop)

This fork exists for autonomous agents to work in parallel and coordinate with each other. Humans upstream changes to `status-im/status-desktop` manually; agents never do.

## Vocabulary

- **Canonical config** — the committed `CLAUDE.md`, `AGENTS.md`, `CONTEXT.md`, `docs/adr/`, and `.claude/settings.json` on this fork's `master`. The `~/Repos/status-desktop` upstream clone symlinks to these files; edit them here, never there.
- **Fork-internal PR** — a PR from a branch of this fork against this fork's `master`. The only kind of PR agents open.
- **Config commits** — commits touching only canonical config. Keep them separate from feature commits so they're easy to cherry-pick or rebase.

## Coordination layers

Two layers, used together:

1. **GitHub (durable state)** — the source of truth for what's being worked on.
   - Issues on `alexjba/status-desktop` = task board. Claim an issue by assigning/commenting before starting.
   - PRs = handoff and review. Agents review each other's PRs via `gh pr review`.
   - `master` = integration point. Merged PR = done.
2. **cmux (live signals)** — for real-time nudges between agents running as cmux workspaces. See `~/.claude/cmux.md` for the full CLI.
   - Report progress: `cmux set-status <key> "<val>"`, `cmux set-progress 0.0-1.0`, `cmux log --level info|success|error "<msg>"`.
   - Alert when blocked or done: `cmux notify --title "..." --body "..."` — this lights the blue ring instead of the human polling.
   - Nudge another agent: `cmux identify --json` first, then `cmux send --surface <id> "<text>"`.
   - cmux state is ephemeral. Anything another agent must not miss goes in a GitHub comment, not just a cmux message.

## Branches and worktrees

- Branch naming: `<type>+<short-slug>` — types: `feat`, `fix`, `perf`, `chore`, `poc` (e.g. `perf+async-wallet`).
- One agent = one worktree under `.claude/worktrees/<branch>`. Never work directly on `master`.
- Canonical config is committed, so every worktree automatically has it.

## Base freshness

`master` is synced with upstream `status-im/status-desktop` **manually by the human** — there is no automation. Before starting significant work:

```bash
git fetch origin && git log -1 --format='%ci %h %s' origin/master
```

If the base looks stale for your task (e.g. you're touching code known to churn upstream), flag it via `cmux notify` and a GitHub issue comment instead of assuming freshness.

## PR conventions

- Target: this fork's `master`. Never `status-im/status-desktop` (see Branch Policy in `CLAUDE.md`).
- Reference the issue being worked (`Closes #N`).
- Keep config commits out of feature PRs unless the PR is about config.
- Before opening: build must pass locally for the affected platform; run relevant tests (`CLAUDE.md` → Testing).

## Sandcastle fleet (containerized agents)

`.sandcastle/` runs unattended agents in Docker via [sandcastle](https://github.com/mattpocock/sandcastle). **The loop's behavior is defined by `.sandcastle/main.mts` and `.sandcastle/*-prompt.md` — those are the source of truth for what container agents do**, with review standards in `.sandcastle/CODING_STANDARDS.md`. This section is only the operator manual; don't duplicate loop mechanics here.

- **Scope:** work verifiable on Linux (Nim/Go/QML code + their tests/lint). Anything needing macOS builds, `make run`, or devices belongs to host agents. Container agents have no cmux access by design.
- **Task board:** label fork issues **`sandcastle`**. The loop works unblocked ones, and the merge phase lands them autonomously: push branch → fork-internal PR (`Closes #N`) → wait for CI → merge when green (auto-merge if CI is slow; PR left open with an issue comment if CI fails).
- **Credentials** (`.sandcastle/.env`, gitignored): `CLAUDE_CODE_OAUTH_TOKEN` from `claude setup-token` (subscription quota — never set `ANTHROPIC_API_KEY` alongside it), plus `GH_TOKEN`: fine-grained PAT scoped to this fork — Contents RW (push), Pull requests RW (merge), Issues RW, Metadata.
- **Run:**

  ```bash
  # once, and after Dockerfile changes (UID/GID args are required — sandcastle
  # runs the container as your host user and expects a matching `agent` user):
  docker build --build-arg AGENT_UID=$(id -u) --build-arg AGENT_GID=$(id -g) \
    -t status-desktop-agent:local .sandcastle
  npm install            # once (repo root)
  npm run sandcastle
  ```

  The merge phase operates on your checkout (it checks branches out to rebase them, and fast-forwards local `master` to origin at the end) — start from a clean `master` and don't work in this checkout while the loop runs. First run in a fresh worktree is slow (submodules + vendor deps).

## Upstreaming (human-only)

Branches cut from fork `master` carry the config commits in their history, so a raw PR against `status-im/status-desktop` would include them. To extract a clean upstream branch, transplant only the branch's own commits:

```bash
git fetch upstream
git rebase --onto upstream/master origin/master <branch>
```

The config commits stay behind automatically — they're part of the base, not the branch.

## Durable knowledge

Anything learned that outlives the task goes in committed docs, not per-checkout memory:

- Domain terms → `CONTEXT.md`
- Hard-to-reverse decisions with real trade-offs → `docs/adr/`
- Build/tooling facts, conventions → `CLAUDE.md`
