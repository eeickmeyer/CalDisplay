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

## Installation (Snap)

**Build the snap:**
```
sudo apt install -y snapcraft
snapcraft
```

**Install locally:**
```
sudo snap install --dangerous ./caldisplay_1.0.0_amd64.snap
```

**Install from Snap Store (after publishing):**
```
sudo snap install caldisplay
```

## Kiosk Usage

CalDisplay runs as a user-scoped snap service (starts on login, stops on logout). This requires enabling a snapd feature flag once:

```
sudo snap set system experimental.user-daemons=true
```

After enabling the flag, install the snap and manage the service with:

**Enable kiosk autostart at login:**
```
snap start --enable caldisplay
```

**Disable autostart:**
```
snap stop --disable caldisplay
```

**Start/stop manually:**
```
snap start caldisplay
snap stop caldisplay
```

**Open configuration (windowed mode):**
```
caldisplay.caldisplay-windowed
```

The kiosk runs as a user daemon (`systemd --user`), starting after the graphical session is ready. Disable screen sleep in your desktop's power settings for a permanent kiosk display.

## Build from Source (Development)

```
sudo apt install -y build-essential cmake qt6-base-dev qt6-declarative-dev
cmake -S . -B build
cmake --build build -j$(nproc)
./build/CalDisplay            # full-screen kiosk
./build/CalDisplay --windowed # configuration mode
```

## Next Implementation Milestones

1. Add CalDAV account configuration (server URL, username, app password/token).
2. Implement incremental sync using WebDAV sync-token when available.
3. Add ETag/time-range fallback for servers without sync-token support.
4. Persist cache in SQLite for offline continuity.
5. Add reminder panel and optional audible alerts.
