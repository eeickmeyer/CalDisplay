# Architecture Notes

## Performance Priorities for Old Hardware

- Keep the UI mostly static and avoid expensive effects.
- Update only changed rows when possible.
- Separate sync work from UI thread.
- Keep refresh intervals predictable.

## Planned Modules

- UI Layer (QML): dashboard and reminders.
- Data Model (C++): efficient list model exposed to QML.
- Sync Engine (C++): CalDAV discovery, query, and incremental sync.
- Storage (SQLite): local cache for offline operation.

## Sync Strategy

1. Discover calendars and capabilities.
2. If sync-token is available, run incremental sync.
3. Otherwise use time-range + ETag diff strategy.
4. Apply inserts/updates/deletes to cache.
5. Emit model updates to refresh dashboard.
