#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
uid=$(id -u)
gid=$(id -g)

sudo -n docker run --rm \
  -e HOST_UID="$uid" \
  -e HOST_GID="$gid" \
  -v "$root:/src" \
  -w /src \
  debian:12 \
  bash -euc '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends build-essential cmake libboost-all-dev nlohmann-json3-dev ca-certificates
    rm -rf /var/lib/apt/lists/*
    groupadd -g "$HOST_GID" builder || true
    useradd -m -u "$HOST_UID" -g "$HOST_GID" builder || true
    chown -R builder:builder /src
    su -s /bin/bash builder -c "cmake -S /src -B /src/build-debian12 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON"
    su -s /bin/bash builder -c "cmake --build /src/build-debian12 --parallel 2"
    su -s /bin/bash builder -c "ctest --test-dir /src/build-debian12 --output-on-failure"
  '
