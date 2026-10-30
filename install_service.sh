#!/usr/bin/bash
set -euo pipefail

CAN_SERVICE="can_starter.service"
SENDER_SERVICE="sender.service"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

sudo install -m 0644 \
    "$SCRIPT_DIR/systemd/$SENDER_SERVICE" \
    "$SCRIPT_DIR/systemd/$CAN_SERVICE" \
    "/etc/systemd/system/"

sudo systemctl daemon-reload

sudo systemctl enable "$SENDER_SERVICE" "$CAN_SERVICE"
sudo systemctl restart "$CAN_SERVICE"
sudo systemctl restart "$SENDER_SERVICE"

sudo systemctl status "$CAN_SERVICE" --no-pager
sudo systemctl status "$SENDER_SERVICE" --no-pager
