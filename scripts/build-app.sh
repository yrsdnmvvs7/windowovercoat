#!/bin/zsh
# Package the knit border daemon as a proper menu bar app bundle.
# LSUIElement keeps it out of the Dock. Ad-hoc signing verifies the local
# bundle; it is not Developer ID signing or notarization for distribution.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/outputs/Window Overcoat.app"
STAGE="$(mktemp -d /tmp/window-overcoat-build.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT

python3 "$ROOT/scripts/update-icons.py" >/dev/null
make -C "$ROOT" >/dev/null

STAGED="$STAGE/Window Overcoat.app/Contents"
mkdir -p "$STAGED/MacOS"
cp -f "$ROOT/bin/borders"     "$STAGED/MacOS/WindowOvercoat"
cp -f "$ROOT/AppInfo.plist"   "$STAGED/Info.plist"
mkdir -p "$STAGED/Resources"
cp -R "$ROOT"/en.lproj "$ROOT"/zh-Hans.lproj "$STAGED/Resources/"
cp -f "$ROOT/assets/AppIcon.icns"    "$STAGED/Resources/AppIcon.icns"
cp -f "$ROOT/assets/MenuBarIcon.png" "$STAGED/Resources/MenuBarIcon.png"
cp -f "$ROOT/assets/MenuBarIcon@2x.png" "$STAGED/Resources/MenuBarIcon@2x.png"
xcrun actool "$ROOT/assets/Assets.xcassets" --compile "$STAGED/Resources" \
  --platform macosx --minimum-deployment-target 13.0 --app-icon AppIcon \
  --output-partial-info-plist "$STAGE/IconInfo.plist" >/dev/null
chmod +x "$STAGED/MacOS/WindowOvercoat"

xattr -cr "$STAGE/Window Overcoat.app"
codesign --force --deep --sign - "$STAGE/Window Overcoat.app"
codesign --verify --deep --strict "$STAGE/Window Overcoat.app"

mkdir -p "$ROOT/outputs"
rm -rf "$APP"
ditto "$STAGE/Window Overcoat.app" "$APP"
echo "$APP"
