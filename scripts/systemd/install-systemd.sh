#!/usr/bin/env bash
# Install RuView systemd services and enable them to start on boot.
# Run as a regular user — uses sudo only for systemctl commands.
#
# Usage:
#   ./scripts/systemd/install-systemd.sh [--uninstall] [--status]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

UNITS=(ruview-sensing.service ruview-api.service)
SYSTEMD_DIR=/etc/systemd/system

case "${1:-install}" in
    --uninstall)
        echo "Stopping and disabling RuView services..."
        for unit in "${UNITS[@]}"; do
            sudo systemctl stop "$unit" 2>/dev/null || true
            sudo systemctl disable "$unit" 2>/dev/null || true
            sudo rm -f "$SYSTEMD_DIR/$unit"
            echo "  Removed $unit"
        done
        sudo systemctl daemon-reload
        echo "Uninstalled."
        exit 0
        ;;
    --status)
        for unit in "${UNITS[@]}"; do
            echo "─── $unit ───"
            systemctl status "$unit" --no-pager --lines=5 2>/dev/null || echo "  Not installed"
            echo ""
        done
        exit 0
        ;;
    install)
        ;;
    *)
        echo "Usage: $0 [--uninstall|--status]"
        exit 1
        ;;
esac

# Pre-flight checks
SENSING_BIN="$PROJ_DIR/rust-port/wifi-densepose-rs/target/release/sensing-server"
if [ ! -x "$SENSING_BIN" ]; then
    echo "ERROR: sensing-server binary not found at $SENSING_BIN"
    echo "  Run first:  bash $PROJ_DIR/scripts/build-nobara.sh"
    exit 1
fi

if [ ! -f "$PROJ_DIR/.env" ] && [ -f "$PROJ_DIR/example.env" ]; then
    echo "Creating .env from example.env..."
    cp "$PROJ_DIR/example.env" "$PROJ_DIR/.env"
fi

# Install unit files
echo "Installing RuView systemd services..."
for unit in "${UNITS[@]}"; do
    sudo cp "$SCRIPT_DIR/$unit" "$SYSTEMD_DIR/$unit"
    echo "  Installed $SYSTEMD_DIR/$unit"
done

sudo systemctl daemon-reload

# Enable and start services
for unit in "${UNITS[@]}"; do
    sudo systemctl enable "$unit"
    sudo systemctl restart "$unit"
    echo "  Started $unit"
done

echo ""
echo "Services installed and running."
echo ""
echo "  Check status:  bash $0 --status"
echo "  View logs:     journalctl -u ruview-sensing -f"
echo "  View logs:     journalctl -u ruview-api -f"
echo "  UI:            http://localhost:3000"
echo "  API docs:      http://localhost:8000/docs"
echo "  WebSocket:     ws://localhost:3001"
