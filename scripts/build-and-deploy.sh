#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."

# Build the local binary first (catches compile errors fast)
echo "==> Building locally (check)..."
make -C "$ROOT_DIR/kindle/native" check

# Cross-compile the ARM extension via Zig
echo "==> Cross-compiling extension (zig)..."
make -C "$ROOT_DIR/kindle/native" extension-zig

# Deploy to mounted Kindle
echo "==> Deploying to Kindle..."
npm run --prefix "$ROOT_DIR" native:install

echo "==> Done."
