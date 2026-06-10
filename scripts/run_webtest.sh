#!/usr/bin/env bash
# Run the Node.js dev mock server for local web UI testing.
# Usage: ./scripts/run_webtest.sh [--dev] [--port <PORT>]
#   --dev        : use nodemon (auto-restart on file change)
#   --port <N>   : listen port (default: 3000)

set -euo pipefail

# ── Resolve project root from script location ────────────────────────────────
if [[ "$OSTYPE" == "darwin"* ]]; then
    SCRIPT_DIR=$(dirname "$(realpath "$0")")
else
    SCRIPT_DIR=$(dirname "$(realpath "${BASH_SOURCE[0]}")")
fi
PROJECT_ROOT=$(dirname "$SCRIPT_DIR")
TEST_DIR="${PROJECT_ROOT}/test"

# ── Parse arguments ──────────────────────────────────────────────────────────
USE_DEV=false
PORT=3000

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dev)   USE_DEV=true; shift ;;
        --port)  PORT="$2"; shift 2 ;;
        *)       echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Sanity checks ────────────────────────────────────────────────────────────
if ! command -v node &>/dev/null; then
    echo "[ERROR] node is not installed or not in PATH"
    exit 1
fi

if [[ ! -f "${TEST_DIR}/server.js" ]]; then
    echo "[ERROR] ${TEST_DIR}/server.js not found"
    exit 1
fi

# ── Install dependencies if needed ───────────────────────────────────────────
if [[ ! -d "${TEST_DIR}/node_modules" ]]; then
    echo "[INFO] node_modules not found — running npm install..."
    (cd "$TEST_DIR" && npm install)
fi

# ── Print info ───────────────────────────────────────────────────────────────
echo "------------------------------------------------------"
echo " Sink IoT Web Dev Server"
echo "------------------------------------------------------"
echo " Project : ${PROJECT_ROOT}"
echo " Node    : $(node --version)"
echo " Port    : ${PORT}"
echo " Mode    : $([ "$USE_DEV" = true ] && echo 'nodemon (watch)' || echo 'node (oneshot)')"
echo "------------------------------------------------------"
echo ""

# ── Launch ───────────────────────────────────────────────────────────────────
cd "$TEST_DIR"

if $USE_DEV; then
    if ! command -v npx &>/dev/null; then
        echo "[ERROR] npx is not available"
        exit 1
    fi
    exec npx nodemon server.js "$PORT"
else
    exec node server.js "$PORT"
fi
