FROM harbor.status.im/status-im/status-desktop-build:1.0.10-qt6.11.0-arm64

# The harbor base's default user is jenkins; package installs need root.
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


# Build-args for UID/GID alignment: sandcastle docker build-image
# defaults these to the host user's UID/GID so image-built files
# and bind-mounted files share an owner without runtime chown.
ARG AGENT_UID=1000
ARG AGENT_GID=1000

# The harbor base has no "node" user (that rename trick is for node:* images);
# create the "agent" user sandcastle expects, aligned to the host UID/GID.
RUN (getent group $AGENT_GID || groupadd -g $AGENT_GID agent) \
 && useradd -o -u $AGENT_UID -g $AGENT_GID -m -d /home/agent -s /bin/bash agent
USER ${AGENT_UID}:${AGENT_GID}
# The harbor base bakes ENV HOME=/home/jenkins; override BEFORE the claude
# installer below, which writes to $HOME/.local.
ENV HOME=/home/agent

# Install Claude Code CLI
RUN curl -fsSL https://claude.ai/install.sh | bash

# Add Claude to PATH
ENV PATH="/home/agent/.local/bin:$PATH"

WORKDIR /home/agent

# In worktree sandbox mode, Sandcastle bind-mounts the git worktree at /home/agent/workspace
# and overrides the working directory to /home/agent/workspace at container start.
# Structure your Dockerfile so that /home/agent/workspace can serve as the project root.
ENTRYPOINT ["sleep", "infinity"]
