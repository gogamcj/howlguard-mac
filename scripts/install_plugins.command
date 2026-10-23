#!/bin/bash
# Double-click in Finder: installs the HowlGuard plug-ins for the current user
# and removes the download quarantine flag so macOS will load them.
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

xattr -dr com.apple.quarantine HowlGuard.app HowlGuard.vst3 HowlGuard.component 2>/dev/null

rm -rf "$VST3_DIR/HowlGuard.vst3" "$AU_DIR/HowlGuard.component"
cp -R HowlGuard.vst3 "$VST3_DIR/"
cp -R HowlGuard.component "$AU_DIR/"

# make Logic/GarageBand rescan Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null

echo "설치 완료:"
echo "  VST3 -> $VST3_DIR/HowlGuard.vst3"
echo "  AU   -> $AU_DIR/HowlGuard.component"
echo "HowlGuard.app 은 응용 프로그램 폴더로 옮겨서 쓰시면 됩니다."
echo "호스트 프로그램에서 플러그인을 다시 스캔하세요."
read -n 1 -s -r -p "아무 키나 누르면 닫힙니다."
