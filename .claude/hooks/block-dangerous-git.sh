#!/bin/bash
# PreToolUse guardrail for the agent fleet: blocks destructive git commands.
# Plain `git push` and `gh pr create` stay allowed — fork-internal PR workflow
# depends on them (see docs/AGENT-WORKFLOW.md).

INPUT=$(cat)
COMMAND=$(echo "$INPUT" | jq -r '.tool_input.command')

DANGEROUS_PATTERNS=(
  "push[^|;&]*--force"
  "push[^|;&]*-f\b"
  "push[^|;&]* \+"
  "push[^|;&]*--delete"
  "push[^|;&]*--mirror"
  "reset --hard"
  "git clean -[a-zA-Z]*f"
  "git branch -D"
  "git branch [^|;&]*--delete[^|;&]*--force"
  "git checkout \."
  "git restore \."
  "git checkout -- \."
  "worktree remove[^|;&]*--force"
  "update-ref -d"
  "reflog expire"
  "filter-branch"
)

for pattern in "${DANGEROUS_PATTERNS[@]}"; do
  if echo "$COMMAND" | grep -qE "$pattern"; then
    echo "BLOCKED: '$COMMAND' matches dangerous pattern '$pattern'. The user has prevented you from doing this. Plain 'git push' and PRs are allowed; destructive rewrites are not." >&2
    exit 2
  fi
done

exit 0
