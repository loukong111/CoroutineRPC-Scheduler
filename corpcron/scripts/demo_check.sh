#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

DOCKER="${DOCKER:-docker}"

echo "== Binaries =="
for bin in build/corpcron_server build/client/corpcron_client build/test_submit_client build/bench_client; do
    if [[ -x "${bin}" ]]; then
        echo "OK   ${bin}"
    else
        echo "MISS ${bin}"
    fi
done

echo
echo "== Compose services =="
if "${DOCKER}" compose ps >/tmp/corpcron-compose-ps.txt 2>/tmp/corpcron-compose-err.txt; then
    cat /tmp/corpcron-compose-ps.txt
else
    cat /tmp/corpcron-compose-err.txt
fi

echo
echo "== Expected ports =="
echo "Redis: 127.0.0.1:6380"
echo "MySQL: 127.0.0.1:3307"
echo "RPC:   127.0.0.1:8081"

echo
echo "== Useful commands =="
echo "Start dependencies: ${DOCKER} compose up -d"
echo "Start server:       CORPCRON_RPC_AUTH_TOKEN=demo ./build/corpcron_server --config config/server.conf"
echo "Start Qt client:    ./build/client/corpcron_client"
