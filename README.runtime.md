# theta-spxw-filter runtime package

This package contains a compiled Debian 12 x86_64 runtime artifact only. Source code is intentionally not included.

## Start on majula

```bash
export THETA_FILTER_EXPIRATION=YYYYMMDD
./scripts/start-on-majula.sh
```

Default endpoints:

```text
FPSS WebSocket: ws://172.18.0.2:25520/v1/events
HTTP health/events: http://127.0.0.1:25521
```

The listener is loopback-only. Inspect it locally on majula:

```bash
curl -fsS http://127.0.0.1:25521/healthz
curl -fsS 'http://127.0.0.1:25521/events?limit=20'
curl -fsS 'http://127.0.0.1:25521/events.pb?limit=20' -o events.pb
```

For a detached runtime:

```bash
mkdir -p logs
tmux new-session -d -s theta-spxw-filter \
  'THETA_FILTER_EXPIRATION=YYYYMMDD ./scripts/start-on-majula.sh >logs/filter.log 2>&1'
```

The filter subscribes to the single FPSS full option trade stream and publishes only target `TRADE` and `QUOTE` events whose contract root and expiration match the configured target. `GET /events` returns wrapped NDJSON; `GET /events.pb?limit=N` returns a `theta_filter.v1.EventBatch` protobuf with `schema_version = 1` and `Content-Type: application/x-protobuf`. It archives forwarded wrapped NDJSON to `archive/<expiration>/events.ndjson` by default.

For lightweight forwarding without disk writes, start it with:

```bash
THETA_FILTER_NO_ARCHIVE=1 ./scripts/start-on-majula.sh
```

This keeps only the bounded in-memory ring exposed through `/events` and `/events.pb`.

It does not calculate inventory, infer customer/dealer intent, or emit trading advice.
