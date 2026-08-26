#!/usr/bin/env bash
set -e

# Project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$ROOT_DIR"

# Ensure build directory and binary are up to date
mkdir -p build
cmake -B build
cmake --build build --target razor_router_daemon

CONFIG_FILE="${1:-model.yaml}"

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file '$CONFIG_FILE' not found."
    exit 1
fi

echo "Starting Razor Router Daemon using config: $CONFIG_FILE..."
exec ./build/router/razor_router_daemon "$CONFIG_FILE"
