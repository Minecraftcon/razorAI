#!/usr/bin/env python3
import os
import time
import sys

LOG_FILE = os.path.expanduser("~/.razor/daemon.log")

# ANSI color codes
RESET = "\033[0m"
DIM = "\033[2m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
MAGENTA = "\033[35m"
CYAN = "\033[36m"
WHITE = "\033[37m"
BRIGHT_BLUE = "\033[94m"
BRIGHT_MAGENTA = "\033[95m"

def colorize(line):
    # Determine severity based on tags like [INFO], [SUCCESS], [ERROR], [EVENT]
    color = DIM + WHITE
    
    if "[ERROR]" in line:
        color = RED
    elif "[SUCCESS]" in line:
        color = GREEN
    elif "[EVENT]" in line:
        color = BRIGHT_MAGENTA
    elif "[INFO]" in line:
        color = CYAN

    # Highlight timestamps
    if "] [" in line:
        parts = line.split("] [", 1)
        timestamp = DIM + parts[0] + "]" + RESET
        rest = "[" + parts[1]
        
        # Colorize just the tag brightly if possible
        if "] " in rest:
            tag_parts = rest.split("] ", 1)
            tag = color + tag_parts[0] + "]" + RESET
            msg = color + tag_parts[1] + RESET
            return f"{timestamp} {tag} {msg}"
    
    return color + line + RESET

def follow(thefile):
    # Seek to end of file if it already exists
    thefile.seek(0, 2)
    while True:
        line = thefile.readline()
        if not line:
            time.sleep(0.1)
            continue
        yield line

def main():
    print(f"{BRIGHT_BLUE}======================================================={RESET}")
    print(f"{BRIGHT_BLUE}             RazorAI Daemon Log Visualizer             {RESET}")
    print(f"{BRIGHT_BLUE}======================================================={RESET}")
    print(f"Tailing log file: {LOG_FILE}\n")

    # Wait for file to exist
    while not os.path.exists(LOG_FILE):
        print(f"{YELLOW}Waiting for daemon log file to be created...{RESET}", end="\r")
        time.sleep(1)
    
    print(" " * 60, end="\r") # Clear line
    
    try:
        with open(LOG_FILE, "r") as logfile:
            loglines = follow(logfile)
            for line in loglines:
                print(colorize(line.strip()))
    except KeyboardInterrupt:
        print(f"\n{DIM}Exiting log visualizer.{RESET}")
        sys.exit(0)

if __name__ == "__main__":
    main()
