#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH_CLIENT="${ROOT_DIR}/build/bench_client"
RESULT_DIR="${ROOT_DIR}/docs/assets/benchmark"

HOST="${1:-127.0.0.1}"
PORT="${2:-8081}"
CONCURRENCY="${3:-16}"
REQUESTS="${4:-1000}"

if [[ ! -x "${BENCH_CLIENT}" ]]; then
    echo "bench_client not found: ${BENCH_CLIENT}" >&2
    echo "Run: cmake -S . -B build && cmake --build build -j" >&2
    exit 1
fi

mkdir -p "${RESULT_DIR}"

TIMESTAMP="$(date '+%Y%m%d-%H%M%S')"
RESULT_FILE="${RESULT_DIR}/bench-${TIMESTAMP}.txt"
LATEST_FILE="${RESULT_DIR}/latest.txt"

{
    echo "# CorpCron Benchmark Result"
    echo "time=$(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "host=${HOST}"
    echo "port=${PORT}"
    echo "concurrency=${CONCURRENCY}"
    echo "requests=${REQUESTS}"
    echo "command=build/bench_client ${HOST} ${PORT} ${CONCURRENCY} ${REQUESTS}"
    echo
    "${BENCH_CLIENT}" "${HOST}" "${PORT}" "${CONCURRENCY}" "${REQUESTS}"
} | tee "${RESULT_FILE}"

cp "${RESULT_FILE}" "${LATEST_FILE}"

echo
echo "saved_to=${RESULT_FILE}"
echo "latest=${LATEST_FILE}"
