#!/usr/bin/env bash
# ==============================================================================
#  Razor AI - Termux & Linux Automated Installer
# ==============================================================================
set -e

echo "============================================================"
echo "           ⚡ Installing Razor AI Engine ⚡"
echo "============================================================"

# Detect Termux environment
if [ -n "$TERMUX_VERSION" ] || [ -d "/data/data/com.termux/files/usr" ]; then
    echo "[*] Detected Termux environment."
    PREFIX="/data/data/com.termux/files/usr"
    BINDIR="$PREFIX/bin"
    
    # Check dependencies on Termux
    echo "[*] Checking Termux dependencies..."
    pkg update -y || true
    pkg install -y clang cmake make git libcurl python || true
else
    echo "[*] Detected standard Linux environment."
    if [ "$(id -u)" -eq 0 ]; then
        PREFIX="/usr/local"
    else
        PREFIX="$HOME/.local"
    fi
    BINDIR="$PREFIX/bin"
fi

mkdir -p "$BINDIR"

# 1. Build and install Razor
echo "[*] Building Razor engine..."
make -j"$(nproc 2>/dev/null || echo 2)" build

echo "[*] Installing binary to $BINDIR/razor..."
install -m 755 build/ui/razor_cpp_standalone "$BINDIR/razor"

# 2. Deploy Skills & Manifest
RAZOR_DIR="$HOME/.razor"
echo "[*] Deploying skills and configuration to $RAZOR_DIR..."
mkdir -p "$RAZOR_DIR/skills" "$RAZOR_DIR/plugins"

if [ -d "assets/skills" ]; then
    cp -rf assets/skills/* "$RAZOR_DIR/skills/" 2>/dev/null || true
fi

if [ -d "assets/plugins" ]; then
    cp -rf assets/plugins/* "$RAZOR_DIR/plugins/" 2>/dev/null || true
fi

if [ -f "assets/skills_manifest.json" ]; then
    cp -f assets/skills_manifest.json "$RAZOR_DIR/" 2>/dev/null || true
fi

# Deploy default config if not existing
if [ ! -f "$RAZOR_DIR/model.yaml" ] && [ -f "model.yaml" ]; then
    cp -f model.yaml "$RAZOR_DIR/model.yaml"
fi

if [ ! -f "$RAZOR_DIR/config.yaml" ] && [ -f "config.yaml" ]; then
    cp -f config.yaml "$RAZOR_DIR/config.yaml"
fi

# Ensure BINDIR is in PATH
if [[ ":$PATH:" != *":$BINDIR:"* ]]; then
    echo "[!] Note: $BINDIR is not in your current PATH."
    echo "    Add it by running: export PATH=\"$BINDIR:\$PATH\""
fi

echo ""
echo "============================================================"
echo "  ✓ Installation Complete!"
echo "  ✓ Razor Executable : $BINDIR/razor"
echo "  ✓ Skills Installed : $RAZOR_DIR/skills"
echo "  ✓ Launch with      : razor"
echo "============================================================"
