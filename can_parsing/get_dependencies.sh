#!/usr/bin/env bash
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
git clone --recurse-submodules https://github.com/xR3b0rn/dbcppp.git \
    "$SCRIPT_DIR/dbcppp"