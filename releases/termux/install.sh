#!/usr/bin/env bash
# ==============================================================================
#  Razor AI - Termux & Linux Automated Installer
# ==============================================================================
set -e

# Default settings
INSTALL_PREFIX=""
LEGACY_CMAKE=false
FORCE_CONFIG=false
INSTALL_DEPS=true
CLEAN_BUILD=false

# Help menu
show_help() {
    cat << EOF
Razor AI Engine - Installer

Usage:
  ./install.sh [OPTIONS]

Options:
  -p, --prefix <DIR>     Set custom install prefix (default: Termux prefix or ~/.local)
  -c, --legacy-cmake     Enable legacy CMake (<=3.14) compatibility flags
  -f, --force            Force overwrite ~/.razor/model.yaml and configuration
      --clean            Wipe build directory before compiling
      --no-deps          Skip package manager dependency installation
  -h, --help             Show this help message

Examples:
  ./install.sh                      # Standard install
  ./install.sh --legacy-cmake       # Build with older CMake versions
  ./install.sh --prefix /opt/razor  # Custom prefix install
EOF
    exit 0
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--prefix)
            INSTALL_PREFIX="$2"
            shift 2
            ;;
        -c|--legacy-cmake|--old-cmake)
            LEGACY_CMAKE=true
            shift
            ;;
        -f|--force)
            FORCE_CONFIG=true
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --no-deps)
            INSTALL_DEPS=false
            shift
            ;;
        -h|--help)
            show_help
            ;;
        *)
            echo "[!] Unknown option: $1"
            show_help
            ;;
    esac
done

echo "============================================================"
echo "           ⚡ Installing Razor AI Engine ⚡"
echo "============================================================"

# Detect Termux environment
if [ -n "$TERMUX_VERSION" ] || [ -d "/data/data/com.termux/files/usr" ]; then
    IS_TERMUX=true
    DEFAULT_PREFIX="/data/data/com.termux/files/usr"
    echo "[*] Detected Termux environment."
else
    IS_TERMUX=false
    if [ "$(id -u)" -eq 0 ]; then
        DEFAULT_PREFIX="/usr/local"
    else
        DEFAULT_PREFIX="$HOME/.local"
    fi
    echo "[*] Detected standard Linux environment."
fi

PREFIX="${INSTALL_PREFIX:-$DEFAULT_PREFIX}"
BINDIR="$PREFIX/bin"
RAZOR_DIR="$HOME/.razor"

echo "[*] Install prefix: $PREFIX"
echo "[*] Target binary : $BINDIR/razor"
echo "[*] Razor home    : $RAZOR_DIR"

# 1. Check and install dependencies if requested
if [ "$INSTALL_DEPS" = true ]; then
    if [ "$IS_TERMUX" = true ] && command -v pkg >/dev/null 2>&1; then
        echo "[*] Updating Termux packages and installing build dependencies..."
        pkg update -y || true
        pkg install -y clang cmake make git libcurl python || true
    elif command -v apt-get >/dev/null 2>&1 && [ "$(id -u)" -eq 0 ]; then
        echo "[*] Installing build dependencies via apt..."
        apt-get update -y || true
        apt-get install -y build-essential cmake clang libcurl4-openssl-dev git || true
    fi
fi

# Verify required tools
for tool in cmake make clang++ c++; do
    if command -v "$tool" >/dev/null 2>&1; then
        HAS_COMPILER=true
        break
    fi
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "[!] Error: 'cmake' is not installed. Please install cmake and rerun."
    exit 1
fi

# Check CMake version
CMAKE_VER=$(cmake --version | head -n1 | grep -oE '[0-9]+\.[0-9]+' | head -n1 || echo "3.10")
echo "[*] CMake version detected: $CMAKE_VER"

CMAKE_FLAGS="-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
if [ "$LEGACY_CMAKE" = true ]; then
    echo "[*] Enabling legacy CMake compatibility flags..."
    CMAKE_FLAGS="$CMAKE_FLAGS -DCMAKE_POLICY_DEFAULT_CMP0135=OLD -DCMAKE_WARN_DEPRECATED=OFF"
fi

if [ "$CLEAN_BUILD" = true ]; then
    echo "[*] Cleaning build directory..."
    rm -rf build
fi

# 2. Build Razor Engine
echo "[*] Configuring and building Razor Engine..."
mkdir -p build
cmake -B build $CMAKE_FLAGS
cmake --build build --parallel "$(nproc 2>/dev/null || echo 2)"

# 3. Install Executable
mkdir -p "$BINDIR"
echo "[*] Installing binary to $BINDIR/razor..."
install -m 755 build/ui/razor_cpp_standalone "$BINDIR/razor"
if [ -f build/router/razor_router_daemon ]; then
    install -m 755 build/router/razor_router_daemon "$BINDIR/razor_router_daemon"
fi

# 4. Deploy Skills, Manifest, and Models to ~/.razor/
echo "[*] Deploying configuration & skills to $RAZOR_DIR..."
mkdir -p "$RAZOR_DIR/skills" "$RAZOR_DIR/plugins" "$RAZOR_DIR/sessions" "$RAZOR_DIR/roles"

if [ -d "assets/skills" ]; then
    cp -rf assets/skills/* "$RAZOR_DIR/skills/" 2>/dev/null || true
fi

if [ -d "assets/plugins" ]; then
    cp -rf assets/plugins/* "$RAZOR_DIR/plugins/" 2>/dev/null || true
fi

if [ -f "assets/skills_manifest.json" ]; then
    cp -f assets/skills_manifest.json "$RAZOR_DIR/skills_manifest.json" 2>/dev/null || true
fi

# Deploy model.yaml and config.yaml to ~/.razor/
if [ "$FORCE_CONFIG" = true ] || [ ! -f "$RAZOR_DIR/model.yaml" ]; then
    if [ -f "model.yaml" ]; then
        cp -f model.yaml "$RAZOR_DIR/model.yaml"
        echo "[+] Deployed model.yaml -> $RAZOR_DIR/model.yaml"
    fi
else
    echo "[*] Preserving existing configuration at $RAZOR_DIR/model.yaml (use -f to overwrite)"
fi

if [ "$FORCE_CONFIG" = true ] || [ ! -f "$RAZOR_DIR/config.yaml" ]; then
    if [ -f "config.yaml" ]; then
        cp -f config.yaml "$RAZOR_DIR/config.yaml"
    fi
fi

# 5. Check PATH
if [[ ":$PATH:" != *":$BINDIR:"* ]]; then
    echo ""
    echo "[!] Notice: $BINDIR is not in your current PATH."
    if [ "$IS_TERMUX" = true ]; then
        echo "    Add to ~/.bashrc: export PATH=\"$BINDIR:\$PATH\""
    else
        echo "    Add to ~/.bashrc: export PATH=\"$BINDIR:\$PATH\""
    fi
fi

echo ""
echo "============================================================"
echo "  ✓ Razor AI Engine Installed Successfully!"
echo "  ✓ Binary Path       : $BINDIR/razor"
echo "  ✓ Config Location   : $RAZOR_DIR/model.yaml"
echo "  ✓ Skills Directory  : $RAZOR_DIR/skills"
echo "  ✓ Launch Command    : razor"
echo "============================================================"
