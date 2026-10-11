#!/usr/bin/env python3
"""Install the built app for the current user, preserving identity and settings."""
import os
from pathlib import Path
import plistlib
import signal
import subprocess
import shutil
import time

root = Path(__file__).resolve().parents[1]
source = root / 'outputs/Window Overcoat.app'
apps = Path.home() / 'Applications'
destination = apps / source.name
identifier = 'com.windowovercoat.app'

def info(app):
    return plistlib.loads((app / 'Contents/Info.plist').read_bytes())

if not source.exists():
    raise SystemExit('Build first: ./scripts/build-app.sh')
assert info(source)['CFBundleIdentifier'] == identifier
subprocess.run(['codesign', '--verify', '--deep', '--strict', str(source)], check=True)
existing = [p for p in (destination,) if p.exists()]
for app in existing:
    if info(app).get('CFBundleIdentifier') != identifier:
        raise SystemExit(f'Refusing to replace an unrelated app: {app}')

# Development install: replace the bundle directly, preserving user preferences.
executables = {str(p / 'Contents/MacOS' / info(p)['CFBundleExecutable']) for p in existing + [source]}
# A previously installed copy may run from this repository's build folder.
executables.add(str(root / 'outputs/Window Overcoat.app/Contents/MacOS/WindowOvercoat'))

def running():
    lines = subprocess.check_output(['ps', '-axo', 'pid=,comm='], text=True).splitlines()
    return [int(parts[0]) for line in lines
            if len(parts := line.strip().split(None, 1)) == 2 and parts[1] in executables]

for pid in running():
    os.kill(pid, signal.SIGTERM)
deadline = time.monotonic() + 3
while running() and time.monotonic() < deadline:
    time.sleep(.1)
if running():
    raise SystemExit('The app did not stop; installation left untouched.')

apps.mkdir(exist_ok=True)
lsregister = '/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister'
for app in existing:
    subprocess.run([lsregister, '-u', str(app)], check=True)
    shutil.rmtree(app)
subprocess.run(['ditto', str(source), str(destination)], check=True)
subprocess.run(['codesign', '--verify', '--deep', '--strict', str(destination)], check=True)
subprocess.run([lsregister, '-f', str(destination)], check=True)
subprocess.run(['open', str(destination)], check=True)
print(f'Installed: {destination}')
