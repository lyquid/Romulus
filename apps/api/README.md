# ROMULUS — REST API Server (Future)

This directory will contain the C++ REST API server that wraps `RomulusService`
as HTTP/JSON endpoints for the React/TypeScript web frontend.

## Planned Stack
- **cpp-httplib** — Lightweight HTTP server
- **nlohmann-json** — JSON serialization (already a dependency)
- Links against `romulus_lib` — same as the CLI

## Planned Endpoints
```
GET  /api/systems              → list_systems()
GET  /api/summary              → get_summary()
GET  /api/missing              → get_missing_roms()
GET  /api/duplicates           → get_duplicates()
POST /api/import-dat           → import_dat()
POST /api/scan                 → scan_directory()
POST /api/verify               → verify()
GET  /api/report/:type         → generate_report()
```
