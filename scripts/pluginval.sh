#!/usr/bin/env bash
# Validate the built plugin(s) with pluginval.
#   scripts/pluginval.sh [build-dir] [strictness]
# Downloads a pinned pluginval release into build-dir/pluginval on first use.
# macOS: validates VST3 and AU (the AU is copied to ~/Library/Audio/Plug-Ins/Components first).
# Linux: validates VST3 (needs a display; CI uses xvfb-run).
set -euo pipefail

BUILD_DIR="${1:-build}"
STRICTNESS="${2:-10}"
PLUGINVAL_VERSION="v1.0.4"
CONFIG="${CONFIG:-Release}"
ARTEFACTS="$BUILD_DIR/source/plugin/Foundry_artefacts/$CONFIG"
TOOLS="$BUILD_DIR/pluginval"

case "$(uname -s)" in
    Darwin) ZIP="pluginval_macOS.zip"; BIN="$TOOLS/pluginval.app/Contents/MacOS/pluginval" ;;
    Linux)  ZIP="pluginval_Linux.zip"; BIN="$TOOLS/pluginval" ;;
    *)      ZIP="pluginval_Windows.zip"; BIN="$TOOLS/pluginval.exe" ;;
esac

if [[ ! -x "$BIN" ]]; then
    mkdir -p "$TOOLS"
    curl -sSfL -o "$TOOLS/$ZIP" "https://github.com/Tracktion/pluginval/releases/download/$PLUGINVAL_VERSION/$ZIP"
    (cd "$TOOLS" && unzip -o -q "$ZIP")
fi

run() {
    echo "== pluginval: $1"
    "$BIN" --strictness-level "$STRICTNESS" --validate-in-process --timeout-ms 600000 "$1"
}

run "$ARTEFACTS/VST3/Foundry.vst3"

if [[ "$(uname -s)" == "Darwin" ]]; then
    COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
    mkdir -p "$COMPONENTS"
    rm -rf "$COMPONENTS/Foundry.component"
    cp -R "$ARTEFACTS/AU/Foundry.component" "$COMPONENTS/"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    auval -v aumu Fdr1 Fndy
    run "$COMPONENTS/Foundry.component"
fi
echo "pluginval passed"
