#!/usr/bin/env bash

# Launch the Razor daemon in the background
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
nohup "$SCRIPT_DIR/run_daemon.sh" > /dev/null 2>&1 &
echo "Razor Router Daemon launched in background."
