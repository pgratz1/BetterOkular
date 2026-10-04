#!/bin/bash
# Build and install BetterOkular (Okular with pinned page views) for the
# current user, next to the system Okular. See BETTER_OKULAR.md.
#
# Usage: betterokular/install.sh [--no-deps] [--no-launcher] [--tests]
#
#   --no-deps      don't check/install the build dependencies (needs sudo)
#   --no-launcher  don't install ~/.local/bin/okular-pin and its menu entry
#   --tests        also build the autotests (slower)
#
# Environment overrides:
#   BUILD_DIR  (default ~/okular-build)   - must NOT be inside Dropbox
#   PREFIX     (default ~/okular-install)

set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD_DIR="${BUILD_DIR:-$HOME/okular-build}"
PREFIX="${PREFIX:-$HOME/okular-install}"

WANT_DEPS=1
WANT_LAUNCHER=1
WANT_TESTS=OFF
for arg in "$@"; do
    case "$arg" in
    --no-deps) WANT_DEPS=0 ;;
    --no-launcher) WANT_LAUNCHER=0 ;;
    --tests) WANT_TESTS=ON ;;
    -h | --help)
        sed -n '2,15p' "$0"
        exit 0
        ;;
    *)
        echo "Unknown option: $arg" >&2
        exit 1
        ;;
    esac
done

step() { printf '\n==> %s\n' "$*"; }

# 1. build dependencies -------------------------------------------------------
if [ "$WANT_DEPS" = 1 ]; then
    step "Checking build dependencies"
    if ! sim=$(LC_ALL=C apt-get -s build-dep okular 2>&1); then
        if grep -q "deb-src" <<<"$sim"; then
            cat >&2 <<'EOF'
Source package lists are not enabled, so the build dependencies can't be resolved.
Enable them, then rerun this script:

  sudo sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources
  sudo apt update

(or Discover/Software Sources -> enable "Source code")
EOF
        else
            echo "$sim" >&2
        fi
        exit 1
    fi
    if grep -q '^Inst ' <<<"$sim"; then
        echo "Missing packages: $(grep '^Inst ' <<<"$sim" | awk '{print $2}' | tr '\n' ' ')"
        sudo apt-get build-dep -y okular
    else
        echo "All build dependencies are installed."
    fi
fi

# 2. configure, build, install -----------------------------------------------
case "$(readlink -f "$BUILD_DIR")/" in
"$(readlink -f "$HOME")/Dropbox/"*)
    echo "BUILD_DIR $BUILD_DIR is inside Dropbox, please choose a directory outside of it." >&2
    exit 1
    ;;
esac

if [ -f "$BUILD_DIR/CMakeCache.txt" ] && ! grep -q "^CMAKE_HOME_DIRECTORY:INTERNAL=$SRC_DIR\$" "$BUILD_DIR/CMakeCache.txt"; then
    echo "$BUILD_DIR was configured for a different source directory; remove it or set BUILD_DIR." >&2
    exit 1
fi

step "Configuring ($SRC_DIR -> $BUILD_DIR, installing to $PREFIX)"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_TESTING="$WANT_TESTS" >/dev/null

step "Building (this takes a few minutes the first time)"
cmake --build "$BUILD_DIR"

step "Installing to $PREFIX"
cmake --install "$BUILD_DIR" >/dev/null

# 3. launcher -----------------------------------------------------------------
if [ "$WANT_LAUNCHER" = 1 ]; then
    step "Installing launcher"
    mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications"
    launcher="$HOME/.local/bin/okular-pin"
    sed "s|^PREFIX=.*|PREFIX=\"\${BETTEROKULAR_PREFIX:-$PREFIX}\"|" "$SRC_DIR/betterokular/okular-pin" >"$launcher"
    chmod +x "$launcher"
    sed "s|@LAUNCHER@|$launcher|" "$SRC_DIR/betterokular/okular-pin.desktop.in" >"$HOME/.local/share/applications/okular-pin.desktop"
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
    kbuildsycoca6 >/dev/null 2>&1 || true
    echo "Launcher: $launcher  (menu: \"Okular (Pinned Pages)\")"
fi

step "Done"
echo "Run it with:  okular-pin some.pdf"
echo "Make it the default PDF viewer:  xdg-mime default okular-pin.desktop application/pdf"
