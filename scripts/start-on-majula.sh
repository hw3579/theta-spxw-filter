#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
: "${THETA_FILTER_EXPIRATION:?set THETA_FILTER_EXPIRATION to YYYYMMDD}"

archive_dir=${THETA_FILTER_ARCHIVE_DIR:-"$root/archive/$THETA_FILTER_EXPIRATION"}
exec "$root/bin/theta-spxw-filter" \
  --ws-url "${THETA_FILTER_WS_URL:-ws://172.18.0.2:25520/v1/events}" \
  --listen-host "${THETA_FILTER_LISTEN_HOST:-127.0.0.1}" \
  --listen-port "${THETA_FILTER_LISTEN_PORT:-25521}" \
  --symbol "${THETA_FILTER_SYMBOL:-SPXW}" \
  --expiration "$THETA_FILTER_EXPIRATION" \
  --archive-dir "$archive_dir" \
  --ring-capacity "${THETA_FILTER_RING_CAPACITY:-10000}" \
  --reconnect-ms "${THETA_FILTER_RECONNECT_MS:-1000}" \
  --duration-seconds "${THETA_FILTER_DURATION_SECONDS:-0}"
