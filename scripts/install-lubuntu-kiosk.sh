#!/usr/bin/env bash
set -euo pipefail

APP_DIR="/home/erich/Desktop/CalDisplay"
AUTOSTART_DIR="$HOME/.config/autostart"
SYSTEMD_DIR="$HOME/.config/systemd/user"
ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
APPS_DIR="$HOME/.local/share/applications"

mkdir -p "$AUTOSTART_DIR"
mkdir -p "$SYSTEMD_DIR"
mkdir -p "$ICON_DIR"
mkdir -p "$APPS_DIR"

# Install executable icon
cp "$APP_DIR/assets/caldisplay.svg" "$ICON_DIR/caldisplay.svg"

# Install desktop files
cp "$APP_DIR/deploy/caldisplay.desktop" "$AUTOSTART_DIR/caldisplay.desktop"
cp "$APP_DIR/deploy/caldisplay-app.desktop" "$APPS_DIR/caldisplay.desktop"

# Update Exec path in launcher desktop file to use full path to binary
sed -i "s|^Exec=CalDisplay|Exec=$APP_DIR/build/CalDisplay|" "$APPS_DIR/caldisplay.desktop"

cp "$APP_DIR/deploy/caldisplay.service" "$SYSTEMD_DIR/caldisplay.service"

systemctl --user daemon-reload
systemctl --user enable --now caldisplay.service

echo "Installed application icon, desktop launcher, autostart entry, and enabled systemd user service."
echo "Icon: $ICON_DIR/caldisplay.svg"
echo "Launcher: $APPS_DIR/caldisplay.desktop"
echo "Autostart: $AUTOSTART_DIR/caldisplay.desktop"
