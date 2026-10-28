#!/bin/bash
set -e

# Resolve directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

# ── Generate rolling version number ──────────────────────────────────────────
# Format: YYYYMMDD.HHMM.<git-shortsha>  (falls back to 'dev' outside a git repo)
BUILD_DATE="$(date +%Y%m%d.%H%M)"
GIT_SHA="$(git -C "$SCRIPT_DIR" rev-parse --short HEAD 2>/dev/null || echo 'dev')"
BOT_VERSION="${BUILD_DATE}.${GIT_SHA}"
echo "Generated BOT_VERSION: $BOT_VERSION"

# Write/update BOT_VERSION in .env (insert after the first non-comment line if
# absent, or replace the existing BOT_VERSION= line in place).
ENV_FILE="$SCRIPT_DIR/.env"
if grep -q '^BOT_VERSION=' "$ENV_FILE" 2>/dev/null; then
    sed -i "s|^BOT_VERSION=.*|BOT_VERSION=$BOT_VERSION|" "$ENV_FILE"
else
    # Append after the header block (first blank line after comments), or at end
    sed -i "/^# Stoat++ Bronx Bot Configuration/a BOT_VERSION=$BOT_VERSION" "$ENV_FILE" 2>/dev/null \
        || echo "BOT_VERSION=$BOT_VERSION" >> "$ENV_FILE"
fi
echo "Written BOT_VERSION=$BOT_VERSION to $ENV_FILE"

echo "==========================================="
echo " Building Bronx Bot Podman Image... "
echo "==========================================="
podman build --no-cache --rm -t bronx-stoat -f "$REPO_ROOT/Containerfile" "$REPO_ROOT"

echo ""
echo "==========================================="
echo " Stopping & Replacing Bronx Bot Container... "
echo "==========================================="

# Stop existing container if running
if podman ps -q --filter name=bronx-stoat | grep -q .; then
    echo "Stopping running container..."
    podman stop bronx-stoat
fi

# Remove existing container if it exists
if podman ps -a -q --filter name=bronx-stoat | grep -q .; then
    echo "Removing old container..."
    podman rm bronx-stoat
fi

# Detect primary host IP address for container DB connectivity
HOST_IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
if [ -z "$HOST_IP" ]; then
    HOST_IP="host.containers.internal"
fi
echo "Using DB_HOST: $HOST_IP"

# Start the new container
echo "Starting new container..."
podman run -d --name bronx-stoat --net=host --security-opt label=disable --restart unless-stopped \
    -v /var/lib/mysql/mysql.sock:/var/lib/mysql/mysql.sock \
    -v $XDG_RUNTIME_DIR/pulse/native:$XDG_RUNTIME_DIR/pulse/native \
    -e XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR \
    bronx-stoat

# Prune dangling/untagged images automatically to prevent storage bloat
echo ""
echo "Cleaning up dangling images..."
podman image prune -f

echo ""
echo "==========================================="
echo " Success! Bronx Bot is now running. "
echo "==========================================="
podman ps -f name=bronx-stoat
