#!/bin/bash
set -euo pipefail
version="$1"
stage="$GITHUB_WORKSPACE/stage"
dist="$GITHUB_WORKSPACE/dist"
mkdir -p "$stage" "$dist"
cmake --install build --prefix "$stage"
app="$stage/PhotoScreen.app"
test -d "$app"
mkdir -p "$app/Contents/Resources"
cp THIRD_PARTY_NOTICES.md "$app/Contents/Resources/THIRD_PARTY_NOTICES.md"
"$QT_ROOT_DIR/bin/macdeployqt" "$app" -always-overwrite -codesign=-
codesign --verify --deep --strict --verbose=2 "$app"
lipo -verify_arch arm64 "$app/Contents/MacOS/PhotoScreen"
ditto -c -k --sequesterRsrc --keepParent "$app" "$dist/PhotoScreen-$version-macos-arm64.zip"
image_root="$RUNNER_TEMP/photoscreen-dmg-root"
mountpoint="$RUNNER_TEMP/photoscreen-dmg-mounted"
mkdir -p "$image_root" "$mountpoint"
ditto "$app" "$image_root/PhotoScreen.app"
ln -s /Applications "$image_root/Applications"
hdiutil create -volname PhotoScreen -srcfolder "$image_root" -fs APFS -format UDZO "$dist/PhotoScreen-$version-macos-arm64.dmg"
hdiutil attach "$dist/PhotoScreen-$version-macos-arm64.dmg" -readonly -nobrowse -mountpoint "$mountpoint"
trap 'hdiutil detach "$mountpoint" || true' EXIT
codesign --verify --deep --strict --verbose=2 "$mountpoint/PhotoScreen.app"
hdiutil detach "$mountpoint"
trap - EXIT
