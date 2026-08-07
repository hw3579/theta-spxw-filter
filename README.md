# theta-spxw-filter

Local-source C++20 filter for Theta Terminal v3 FPSS option events.

## Scope

The process owns one WebSocket connection to Theta FPSS, subscribes to the full option trade stream, and forwards only `TRADE` or `QUOTE` messages whose option root and expiration equal the configured target. It serves loopback-only health and retained NDJSON events.

It does not calculate dealer inventory, infer trade direction, identify customer intent, or generate trading signals.

## Build for majula

Source remains local. Build using a local Debian 12 Docker builder; this produces an artifact compatible with majula's Debian 12 x86_64 userspace.

```bash
cmake -S . -B build-local-static -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-local-static --parallel 2
ctest --test-dir build-local-static --output-on-failure
./scripts/package-runtime.sh
```

The deployable archive is:

```text
dist/theta-spxw-filter-linux-amd64.tar.gz
```

It contains only the binary, runtime script, runtime README and manifest; it does not include source.

## Test

```bash
ctest --test-dir build-debian12 --output-on-failure
```

The unit test covers target filtering, date normalization, invalid event rejection and bounded-ring behavior. It does not claim a real Theta subscription.

## Runtime protocol

The runtime defaults to:

```text
ws://172.18.0.2:25520/v1/events
```

and sends:

```json
{"msg_type":"STREAM_BULK","sec_type":"OPTION","req_type":"TRADE","add":true,"id":0}
```

On majula, use the packaged `scripts/start-on-majula.sh` with an explicit `THETA_FILTER_EXPIRATION`. The HTTP service is restricted to `127.0.0.1:25521` and provides:

```text
GET /healthz
GET /events?limit=N
```

Use an SSH local forward to consume those endpoints from HP. See the runtime README bundled with the archive for the detached `tmux` command.
