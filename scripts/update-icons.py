#!/usr/bin/env python3
"""Generate every icon from the approved 1024px artwork, entirely locally."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
assets = root / 'assets'
source = Path(sys.argv[1]).expanduser().resolve() if len(sys.argv) > 1 else assets / 'AppIcon-source.png'
canonical = assets / 'AppIcon-source.png'
if not source.is_file():
    raise SystemExit(f'Icon source not found: {source}')
if source.resolve() != canonical.resolve():
    shutil.copyfile(source, canonical)
source = canonical
catalog = assets / 'Assets.xcassets'
appicon = catalog / 'AppIcon.appiconset'
appicon.mkdir(parents=True, exist_ok=True)
(catalog / 'Contents.json').write_text(json.dumps({'info': {'author': 'xcode', 'version': 1}}, indent=2) + '\n')
entries = []
with tempfile.TemporaryDirectory(prefix='overcoat-icon-') as work:
    iconset = Path(work) / 'AppIcon.iconset'
    iconset.mkdir()
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            pixels = size * scale
            filename = f'icon_{size}x{size}' + ('@2x' if scale == 2 else '') + '.png'
            target = appicon / filename
            if pixels == 1024:
                shutil.copyfile(source, target)
            else:
                subprocess.run(['sips', '-z', str(pixels), str(pixels), str(source), '--out', str(target)], check=True, stdout=subprocess.DEVNULL)
            shutil.copyfile(target, iconset / filename)
            entries.append({'idiom': 'mac', 'size': f'{size}x{size}', 'scale': f'{scale}x', 'filename': filename})
    subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o', str(assets / 'AppIcon.icns')], check=True)
(appicon / 'Contents.json').write_text(json.dumps({'images': entries, 'info': {'author': 'xcode', 'version': 1}}, indent=2) + '\n')
# App Store uses the same opaque 1024px artwork. Keep a convenient standalone copy.
shutil.copyfile(source, assets / 'AppStoreIcon.png')
for scale in (1, 2):
    pixels = 18 * scale
    name = 'MenuBarIcon' + ('@2x' if scale == 2 else '') + '.png'
    subprocess.run(['sips', '-z', str(pixels), str(pixels), str(source), '--out', str(assets / name)], check=True, stdout=subprocess.DEVNULL)
print('Generated AppIcon.icns, macOS asset catalog, AppStoreIcon.png and color menu bar icons.')
