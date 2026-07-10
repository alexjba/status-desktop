// Pilot driver: run one containerized agent against a GitHub issue.
//   cd .sandcastle && npx tsx run-issue.ts <issue-number>
// Requires .sandcastle/.env (see .env.example) and the image built via:
//   docker build -t status-desktop-agent:local .sandcastle
import { run, claudeCode } from "@ai-hero/sandcastle";
import { docker } from "@ai-hero/sandcastle/sandboxes/docker";

const issue = process.argv[2];
if (!issue?.match(/^\d+$/)) {
  console.error("usage: npx tsx run-issue.ts <issue-number>");
  process.exit(1);
}

const result = await run({
  agent: claudeCode("claude-fable-5"),
  sandbox: docker({
    imageName: "status-desktop-agent:local",
    env: {
      GITHUB_TOKEN: process.env.GITHUB_TOKEN!,
      GH_TOKEN: process.env.GITHUB_TOKEN!,
    },
  }),
  branchStrategy: { type: "branch", branch: `agent+issue-${issue}` },
  promptFile: "prompt.md",
  promptArgs: { ISSUE_NUMBER: issue },
});

console.log(`branch:  ${result.branch}`);
console.log(`commits: ${result.commits.map((c) => c.sha).join(", ") || "none"}`);
if (result.logFilePath) console.log(`log:     ${result.logFilePath}`);
