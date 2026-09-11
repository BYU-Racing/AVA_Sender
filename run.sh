#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${SCRIPT_DIR}/.env"

if [[ ! -f "${ENV_FILE}" ]]; then
    echo "Error: .env file not found at ${ENV_FILE}. Run 'cp .env.example .env'"
    exit 1
fi

set -a
source "${ENV_FILE}"
set +a

exec "${SCRIPT_DIR}/build/ava_sender"