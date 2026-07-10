# Sandbox image for containerized fleet agents (see docs/AGENT-WORKFLOW.md).
# Base = the status-desktop CI build image: Qt 6.11 + Nim + Go toolchain,
# so `make tests-nim-linux`, status-go unit tests, and qmllint run natively.
# NOTE: tag is arch-specific — this is the Apple Silicon (arm64) variant;
# on x86 hosts use the same version without the -arm64 suffix.
FROM harbor.status.im/status-im/status-desktop-build:1.0.10-qt6.11.0-arm64

USER root

RUN apt-get update && apt-get install -yq --no-install-recommends \
    curl ca-certificates gnupg \
 && curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
 && curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
      -o /usr/share/keyrings/githubcli-archive-keyring.gpg \
 && echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
      > /etc/apt/sources.list.d/github-cli.list \
 && apt-get update && apt-get install -yq --no-install-recommends nodejs gh \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN npm install -g @anthropic-ai/claude-code

# Base image ships a non-root `jenkins` user; agents run as it so files
# written through the bind mount aren't root-owned on the host.
USER jenkins
