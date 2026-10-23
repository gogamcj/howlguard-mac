#!/bin/bash
# Builds HowlGuard for macOS (Apple Silicon + Intel) and packages it into dist/.
# Needs: Xcode Command Line Tools (xcode-select --install) and CMake (brew install cmake).
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
VERSION="1.2.0"
BUILD="$ROOT/build"
OUT="$ROOT/dist/HowlGuard-$VERSION-mac"

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake가 없습니다. 터미널에서 'brew install cmake' 를 먼저 실행하세요."
    echo "(Homebrew가 없다면 https://brew.sh 에서 설치)"
    exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
    echo "Xcode Command Line Tools가 없습니다. 'xcode-select --install' 을 먼저 실행하세요."
    exit 1
fi

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$BUILD" --config Release -j "$(sysctl -n hw.ncpu)"

ART="$BUILD/HowlGuard_artefacts/Release"
rm -rf "$OUT"
mkdir -p "$OUT"
cp -R "$ART/Standalone/HowlGuard.app" "$OUT/"
cp -R "$ART/VST3/HowlGuard.vst3" "$OUT/"
cp -R "$ART/AU/HowlGuard.component" "$OUT/"
cp "$ROOT/docs/USER_GUIDE_KO_MAC.md" "$OUT/"
cp "$ROOT/scripts/install_plugins.command" "$OUT/"
chmod +x "$OUT/install_plugins.command"

# Ad-hoc signature so Apple Silicon will run it (not notarized: first launch needs right-click > Open).
for b in "$OUT/HowlGuard.app" "$OUT/HowlGuard.vst3" "$OUT/HowlGuard.component"; do
    codesign --force --deep --sign - "$b"
done

(cd "$ROOT/dist" && rm -f "HowlGuard-$VERSION-mac.zip" && ditto -c -k --keepParent "HowlGuard-$VERSION-mac" "HowlGuard-$VERSION-mac.zip")
echo
echo "완료: $ROOT/dist/HowlGuard-$VERSION-mac.zip"
