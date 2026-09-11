#!/usr/bin/bash
set -euo pipefail

SERVICE_NAME="sender.service"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

sudo install \
    -m 0644 \
    "$SCRIPT_DIR/systemd/$SERVICE_NAME" \
    "/etc/systemd/system/$SERVICE_NAME"

sudo systemctl daemon-reload
sudo systemctl enable --now "$SERVICE_NAME"

sudo systemctl status "$SERVICE_NAME" --no-pager
