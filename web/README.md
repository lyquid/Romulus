# ROMULUS — Web Frontend (Future)

This directory will contain the React/TypeScript web frontend for ROMULUS.

## Planned Stack
- **React 18+** with TypeScript
- **Vite** for build tooling
- Communicates with the C++ REST API server (`apps/api/`)

## Setup (when implemented)
```bash
cd web
npm install
npm run dev   # Vite dev server on port 5173
```

The dev server will proxy API requests to the C++ backend on port 8080.
