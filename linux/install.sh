#!/bin/sh
# Installs PassGen for the current user and adds an app-menu entry (search it / double-click it).
set -e; cd "$(dirname "$0")"
[ -f passgen ] || sh build.sh
install -Dm755 passgen "$HOME/.local/bin/passgen"
mkdir -p "$HOME/.local/share/applications"
install -Dm644 passgen.svg "$HOME/.local/share/icons/hicolor/scalable/apps/passgen.svg"
printf '[Desktop Entry]\nType=Application\nName=PassGen\nComment=Password generator\nExec=%s/.local/bin/passgen\nIcon=passgen\nTerminal=false\nCategories=Utility;Security;\nStartupWMClass=passgen\n' "$HOME" > "$HOME/.local/share/applications/passgen.desktop"
echo "Installed to ~/.local/bin/passgen"
