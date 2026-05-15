# CalDisplay

CalDisplay is a Qt/QML calendar and clock display designed for wall screens and low-power Linux hardware.

It runs in two modes:
- Windowed mode for setup and configuration
- Full-screen kiosk mode for display

## Features

- Day, Week, Month, and Two-Month views
- Shared calendar feed support via ICS/webcal URLs
- Direct local `.ics` file import by path or `file://` URL
- Multiple feed entries with optional custom calendar names
- Automatic feed refresh with configurable interval
- Manual refresh from the UI
- 12-hour / 24-hour / host-default time format selection
- Sunday-first week option
- All-day event handling
- Tentative event styling (`STATUS:TENTATIVE`)
- Nextcloud public share URL conversion to ICS export URLs

## Calendar Feed Setup

1. Launch CalDisplay in windowed mode.
2. Open Settings.
3. Under Calendar Sources, add entries as Name + URL rows.
4. Click Save to persist settings.
5. Click Refresh Feeds to fetch events now.

Notes:
- Local `.ics` files can be added directly using an absolute path like `/home/user/calendar.ics` or a `file:///home/user/calendar.ics` URL.
- `webcal://` URLs are automatically converted to `https://`.
- Nextcloud public share links like `https://<host>/index.php/apps/calendar/p/<token>` are automatically converted to `https://<host>/remote.php/dav/public-calendars/<token>?export`.
- Parser support currently focuses on VEVENT fields: `SUMMARY`, `DTSTART`, `DTEND`, and `STATUS`.
- Events are filtered to a display window from the start of the previous month through 60 days ahead.

## Snap Build and Install

Build:

```bash
sudo snap install snapcraft --classic
snapcraft pack
```

Install local snap:

```bash
sudo snap install --dangerous ./caldisplay_1.0.0_amd64.snap
```

Run windowed mode (default app command):

```bash
caldisplay
```

Run kiosk/full-screen mode:

```bash
caldisplay-kiosk
```

## Build from Source

```bash
sudo apt install -y build-essential cmake qt6-base-dev qt6-declarative-dev
cmake -S . -B build
cmake --build build -j"$(nproc)"
./build/CalDisplay --windowed
./build/CalDisplay
```

## License

This project is licensed under GPL-3.0-or-later. See the `LICENSE` file for the full text.
