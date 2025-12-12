#!/usr/bin/env python3
import sys
from pathlib import Path

target = Path(sys.argv[1])
backends = sys.argv[2:]
mimetypes = set()

for backend in backends:
    with open(Path(backend), 'r') as f:
        for line in f:
            mimetypes.add(line.rstrip())

mime = ';'.join(mimetypes)

desktop: str = f"""
[Desktop Entry]
Version=1.0
Type=Application
Comment=Powerful quick image viewer
Name=pqiv
NoDisplay=true
Icon=emblem-photos
TryExec=$(PREFIX)/bin/pqiv
Exec=$(PREFIX)/bin/pqiv %F
MimeType={mime};
Categories=Graphics;
Keywords=Viewer;
"""

with open(target, 'w') as f:
    f.write(desktop)