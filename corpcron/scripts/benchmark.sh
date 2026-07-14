#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH_CLIENT="${ROOT_DIR}/build/bench_client"
RESULT_DIR="${ROOT_DIR}/docs/assets/benchmark"

HOST="${1:-127.0.0.1}"
PORT="${2:-8081}"
CONCURRENCY="${3:-16}"
REQUESTS="${4:-1000}"
MODE="${5:-${CORPCRON_BENCH_MODE:-short}}"
SERVER_PID="${CORPCRON_SERVER_PID:-}"
METRICS_URL="${CORPCRON_METRICS_URL:-http://${HOST}:9091/metrics}"
ALERTS_URL="${CORPCRON_ALERTS_URL:-http://${HOST}:9091/alerts}"

if [[ ! -x "${BENCH_CLIENT}" ]]; then
    echo "bench_client not found: ${BENCH_CLIENT}" >&2
    echo "Run: cmake -S . -B build && cmake --build build -j" >&2
    exit 1
fi

mkdir -p "${RESULT_DIR}"

find_server_pid() {
    if [[ -n "${SERVER_PID}" ]]; then
        echo "${SERVER_PID}"
        return 0
    fi
    pgrep -n -x corpcron_server 2>/dev/null || true
}

print_process_snapshot() {
    local label="$1"
    local pid="$2"
    echo "## ${label}"
    if [[ -z "${pid}" ]]; then
        echo "server_process=not_found"
        return
    fi
    if ps -p "${pid}" >/dev/null 2>&1; then
        ps -p "${pid}" -o pid,pcpu,pmem,rss,vsz,etime,cmd
    else
        echo "server_process=not_running pid=${pid}"
    fi
}

print_metrics_snapshot() {
    local label="$1"
    echo "## ${label}"
    echo "metrics_url=${METRICS_URL}"
    if ! command -v curl >/dev/null 2>&1; then
        echo "metrics=skipped curl_not_found"
        return
    fi
    if ! curl -fsS --max-time 2 "${METRICS_URL}"; then
        echo "metrics=unavailable"
    fi
}

print_alerts_snapshot() {
    local label="$1"
    echo "## ${label}"
    echo "alerts_url=${ALERTS_URL}"
    if ! command -v curl >/dev/null 2>&1; then
        echo "alerts=skipped curl_not_found"
        return
    fi
    if ! curl -fsS --max-time 2 "${ALERTS_URL}"; then
        echo "alerts=unavailable"
    fi
}

TIMESTAMP="$(date '+%Y%m%d-%H%M%S')"
RESULT_FILE="${RESULT_DIR}/bench-${TIMESTAMP}.txt"
LATEST_FILE="${RESULT_DIR}/latest.txt"
SERVER_PID="$(find_server_pid)"

{
    echo "# CorpCron Benchmark Result"
    echo "time=$(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "host=${HOST}"
    echo "port=${PORT}"
    echo "concurrency=${CONCURRENCY}"
    echo "requests=${REQUESTS}"
    echo "mode=${MODE}"
    echo "metrics_url=${METRICS_URL}"
    echo "alerts_url=${ALERTS_URL}"
    echo "command=build/bench_client ${HOST} ${PORT} ${CONCURRENCY} ${REQUESTS} ${MODE}"
    echo
    print_process_snapshot "server_before" "${SERVER_PID}"
    echo
    print_metrics_snapshot "metrics_before"
    echo
    print_alerts_snapshot "alerts_before"
    echo
    echo "## client_result"
    "${BENCH_CLIENT}" "${HOST}" "${PORT}" "${CONCURRENCY}" "${REQUESTS}" "${MODE}"
    echo
    print_process_snapshot "server_after" "${SERVER_PID}"
    echo
    print_metrics_snapshot "metrics_after"
    echo
    print_alerts_snapshot "alerts_after"
} | tee "${RESULT_FILE}"

cp "${RESULT_FILE}" "${LATEST_FILE}"

echo
echo "saved_to=${RESULT_FILE}"
echo "latest=${LATEST_FILE}"
