#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build -DUSE_TLS=OFF -DUSE_OPEN_SSL=0
cmake --build build --parallel 2