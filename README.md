# CalDisplay (Qt/QML)

Lightweight full-screen family calendar dashboard designed for older Linux hardware.

## Current State

This is an MVP shell with:
- Full-screen QML dashboard
- Low-overhead list rendering
- Sample family events
- Auto refresh every 60 seconds
- Manual refresh button
- Nextcloud account setup panel
- CalDAV calendar discovery for Nextcloud accounts
- Read-only shared ICS/webcal link mode (primary)

The next step is replacing sample events with CalDAV sync data.

## Read-Only Mode (Primary)

The app is now designed first for read-only family display usage.

1. Launch the app.
2. Press the Account button.
3. In Read-Only Calendar Sources, paste one shared ICS/webcal URL per line.
4. Press Save Links.
5. Press Refresh Feeds.

Notes:
- `webcal://` links are supported and automatically converted to `https://`.
- Event data from feeds is treated as read-only display data.
- The dashboard currently imports standard VEVENT entries with DTSTART, DTEND, and SUMMARY.
- Automatic periodic feed refresh is supported (default: every 5 minutes).
- Automatic refresh can be enabled/disabled and interval-adjusted in the setup panel.

## Nextcloud Account Setup (Optional Advanced)

1. Launch the app.
2. Press the Account button in the top-right corner.
3. Enter your Nextcloud server URL, username, and app password.
4. Press Save.
5. Press Test & Discover to verify credentials and fetch available calendars.

Recommended server URL format:

- `https://your-nextcloud-host/remote.php/dav/`

If only the host is provided, the app automatically appends `/remote.php/dav/`.

## Build (Lubuntu)

Install dependencies:

For Qt6 systems:
- `sudo apt install -y build-essential cmake qt6-base-dev qt6-declarative-dev`

For Qt5 systems:
- `sudo apt install -y build-essential cmake qtbase5-dev qtdeclarative5-dev`

If you are unsure, this is the recommended first attempt on newer Lubuntu:

- `sudo apt install -y build-essential cmake qt6-base-dev qt6-declarative-dev`

Build and run:

- `cmake -S . -B build`
- `cmake --build build -j2`
- `./build/CalDisplay`

## Quick Install Commands

Run this sequence:

- `sudo apt update`
- `sudo apt install -y build-essential cmake qt6-base-dev qt6-declarative-dev`
- `cd /home/erich/Desktop/CalDisplay`
- `cmake -S . -B build`
- `cmake --build build -j2`

## Launch Modes

The app supports two launch modes:

**Full-screen kiosk (default):**
```
./build/CalDisplay
```

**Windowed configuration mode:**
```
./build/CalDisplay --windowed
# or
./build/CalDisplay -w
```

In windowed mode, the setup panel opens automatically so you can configure feed URLs and refresh settings without entering full-screen.

## Kiosk Usage

The app starts in full-screen. For a kiosk-like setup on Lubuntu:
- Disable screen sleep in power settings.
- Add `CalDisplay` to LXQt autostart.
- Optionally run under a supervisor (systemd user service) for restart-on-crash.

Helper files are included:
- `deploy/caldisplay.desktop`
- `deploy/caldisplay.service`
- `scripts/install-lubuntu-kiosk.sh`

Install kiosk startup helpers:

- `cd /home/erich/Desktop/CalDisplay`
- `./scripts/install-lubuntu-kiosk.sh`

The provided systemd service sets `QT_QUICK_BACKEND=software` for better stability on old GPUs.

## Snap Packaging

Build a snap package for easy distribution across different Linux systems:

**Prerequisites:**
- `sudo apt install -y snapcraft`
- Ensure your project is a git repository: `cd /home/erich/Desktop/CalDisplay && git init`

**Build:**
```
snapcraft
```

This creates `caldisplay_1.0.0_*.snap` in the current directory.

**Install locally:**
```
sudo snap install --dangerous ./caldisplay_1.0.0_*.snap
```

**Launch from snap:**
```
# Full-screen kiosk mode
caldisplay

# Windowed configuration mode
caldisplay.caldisplay-windowed
```

**Install from snapcraft.io (after uploading):**
```
sudo snap install caldisplay
```

The snap includes:
- Qt6 runtime dependencies (statically resolved)
- Software rendering mode by default (`QT_QUICK_BACKEND=software`)
- Both full-screen and windowed entry points
- Home folder access for configuration storage
- Network access for calendar feed downloads

## Next Implementation Milestones

1. Add CalDAV account configuration (server URL, username, app password/token).
2. Implement incremental sync using WebDAV sync-token when available.
3. Add ETag/time-range fallback for servers without sync-token support.
4. Persist cache in SQLite for offline continuity.
5. Add reminder panel and optional audible alerts.
