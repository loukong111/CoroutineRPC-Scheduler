#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESULT_DIR="${ROOT_DIR}/docs/assets/benchmark"
LOG_DIR="${RESULT_DIR}/logs"

HOST="${1:-127.0.0.1}"
CONCURRENCY="${2:-16}"
REQUESTS="${3:-1000}"
MODE="${4:-reuse}"

DOCKER="${DOCKER:-docker}"
MYSQL_CONTAINER="${MYSQL_CONTAINER:-corpcron-mysql}"
REDIS_CONTAINER="${REDIS_CONTAINER:-corpcron-redis}"
MYSQL_USER="${CORPCRON_MYSQL_USER:-corpcron}"
MYSQL_PASSWORD="${CORPCRON_MYSQL_PASSWORD:-corpcron_dev_password}"
MYSQL_DATABASE="${CORPCRON_MYSQL_DATABASE:-corpcron}"
AUTH_TOKEN="${CORPCRON_RPC_AUTH_TOKEN:-}"
START_COMPOSE="${CORPCRON_MULTINODE_START_COMPOSE:-0}"
CLEAN_DATA="${CORPCRON_MULTINODE_CLEAN:-1}"
USE_EXISTING_SERVERS="${CORPCRON_MULTINODE_USE_EXISTING:-0}"

SERVER1_PORT="${CORPCRON_SERVER1_PORT:-8081}"
SERVER2_PORT="${CORPCRON_SERVER2_PORT:-8082}"
METRICS1_URL="http://${HOST}:9091"
METRICS2_URL="http://${HOST}:9092"

mkdir -p "${RESULT_DIR}" "${LOG_DIR}"

TIMESTAMP="$(date '+%Y%m%d-%H%M%S')"
RESULT_FILE="${RESULT_DIR}/multinode-${TIMESTAMP}.md"
LATEST_FILE="${RESULT_DIR}/multinode-latest.md"
SERVER1_LOG="${LOG_DIR}/server1-${TIMESTAMP}.log"
SERVER2_LOG="${LOG_DIR}/server2-${TIMESTAMP}.log"
SERVER1_PID=""
SERVER2_PID=""

need_executable() {
    local path="$1"
    if [[ ! -x "${path}" ]]; then
        echo "missing executable: ${path}" >&2
        echo "Run: cmake -S . -B build && cmake --build build -j" >&2
        exit 1
    fi
}

cleanup() {
    if [[ "${USE_EXISTING_SERVERS}" == "1" ]]; then
        return
    fi
    if [[ -n "${SERVER1_PID}" ]] && kill -0 "${SERVER1_PID}" 2>/dev/null; then
        kill "${SERVER1_PID}" 2>/dev/null || true
        wait "${SERVER1_PID}" 2>/dev/null || true
    fi
    if [[ -n "${SERVER2_PID}" ]] && kill -0 "${SERVER2_PID}" 2>/dev/null; then
        kill "${SERVER2_PID}" 2>/dev/null || true
        wait "${SERVER2_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

wait_http() {
    local url="$1"
    local name="$2"
    for _ in $(seq 1 30); do
        if curl -fsS --max-time 1 "${url}/health" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    echo "timeout waiting for ${name}: ${url}/health" >&2
    return 1
}

mysql_exec() {
    local sql="$1"
    "${DOCKER}" exec -i "${MYSQL_CONTAINER}" mysql \
        -u"${MYSQL_USER}" -p"${MYSQL_PASSWORD}" "${MYSQL_DATABASE}" \
        --batch --skip-column-names -e "${sql}"
}

redis_exec() {
    "${DOCKER}" exec -i "${REDIS_CONTAINER}" redis-cli "$@"
}

insert_due_task() {
    local task_id="$1"
    local params="$2"
    mysql_exec "INSERT INTO tasks (id, cron_expr, params, handler, status, next_run_at, retry_count, max_retries) VALUES ('${task_id}', '0 0 0 1 1 ?', '${params}', 'Echo', 1, '2000-01-01 00:00:00', 0, 3);"
}

history_count() {
    local task_id="$1"
    mysql_exec "SELECT COUNT(*) FROM task_history WHERE task_id='${task_id}';" | tr -d '[:space:]'
}

history_nodes() {
    local task_id="$1"
    mysql_exec "SELECT COALESCE(GROUP_CONCAT(DISTINCT exec_node ORDER BY exec_node SEPARATOR ','), '') FROM task_history WHERE task_id='${task_id}';"
}

wait_history_count() {
    local task_id="$1"
    local expected="$2"
    local timeout="$3"
    for _ in $(seq 1 "${timeout}"); do
        local count
        count="$(history_count "${task_id}")"
        if [[ "${count}" -ge "${expected}" ]]; then
            return 0
        fi
        sleep 1
    done
    return 1
}

print_process() {
    local label="$1"
    local pid="$2"
    echo "### ${label}"
    if [[ -n "${pid}" ]] && ps -p "${pid}" >/dev/null 2>&1; then
        ps -p "${pid}" -o pid,pcpu,pmem,rss,vsz,etime,cmd
    else
        echo "process=not_running"
    fi
    echo
}

print_http_snapshot() {
    local label="$1"
    local base_url="$2"
    echo "### ${label}"
    echo "health:"
    curl -fsS --max-time 2 "${base_url}/health" || echo "unavailable"
    echo
    echo "alerts:"
    curl -fsS --max-time 2 "${base_url}/alerts" || echo "unavailable"
    echo
    echo "metrics excerpt:"
    curl -fsS --max-time 2 "${base_url}/metrics" | grep -E 'corpcron_(rpc_requests_total|rpc_error_total|task_success_total|task_failure_total|lock_acquire_success_total|lock_acquire_failure_total|task_duration_p99_ms|schedule_delay_p99_ms)' || echo "unavailable"
    echo
}

print_redis_snapshot() {
    echo "### Redis 服务发现"
    echo 'SMEMBERS services:rpc'
    redis_exec SMEMBERS services:rpc || true
    echo
    echo 'KEYS services:rpc:*'
    redis_exec KEYS 'services:rpc:*' || true
    echo
}

need_executable "${ROOT_DIR}/build/corpcron_server"
need_executable "${ROOT_DIR}/build/bench_client"

cd "${ROOT_DIR}"

if [[ "${START_COMPOSE}" == "1" ]]; then
    "${DOCKER}" compose up -d
fi

if [[ "${CLEAN_DATA}" == "1" ]]; then
    ./scripts/clean_demo_data.sh >/dev/null
fi

if [[ "${USE_EXISTING_SERVERS}" != "1" ]]; then
    CORPCRON_RPC_AUTH_TOKEN="${AUTH_TOKEN}" \
    CORPCRON_MYSQL_PASSWORD="${MYSQL_PASSWORD}" \
    CORPCRON_SERVER_LISTEN_PORT="${SERVER1_PORT}" \
        ./build/corpcron_server --config config/server.conf >"${SERVER1_LOG}" 2>&1 &
    SERVER1_PID="$!"

    CORPCRON_RPC_AUTH_TOKEN="${AUTH_TOKEN}" \
    CORPCRON_MYSQL_PASSWORD="${MYSQL_PASSWORD}" \
        ./build/corpcron_server --config config/server2.conf >"${SERVER2_LOG}" 2>&1 &
    SERVER2_PID="$!"
else
    SERVER1_PID="$(pgrep -f "corpcron_server.*server.conf" | head -n 1 || true)"
    SERVER2_PID="$(pgrep -f "corpcron_server.*server2.conf" | head -n 1 || true)"
fi

wait_http "${METRICS1_URL}" "server1 metrics"
wait_http "${METRICS2_URL}" "server2 metrics"
sleep 3

TASK_ONCE="multinode-once-${TIMESTAMP}"
TASK_FAILOVER="multinode-failover-${TIMESTAMP}"

{
    echo "# CorpCron 多节点压测报告"
    echo
    echo "- time: $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "- node1: ${HOST}:${SERVER1_PORT}, metrics: ${METRICS1_URL}"
    echo "- node2: ${HOST}:${SERVER2_PORT}, metrics: ${METRICS2_URL}"
    echo "- benchmark: concurrency=${CONCURRENCY}, requests=${REQUESTS}, mode=${MODE}"
    echo "- clean_data: ${CLEAN_DATA}"
    echo

    echo "## 1. 启动状态"
    print_process "server1" "${SERVER1_PID}"
    print_process "server2" "${SERVER2_PID}"
    print_redis_snapshot
    print_http_snapshot "server1 before" "${METRICS1_URL}"
    print_http_snapshot "server2 before" "${METRICS2_URL}"

    echo "## 2. RPC 压测"
    echo "### node1 bench_client"
    CORPCRON_RPC_AUTH_TOKEN="${AUTH_TOKEN}" ./build/bench_client "${HOST}" "${SERVER1_PORT}" "${CONCURRENCY}" "${REQUESTS}" "${MODE}"
    echo
    echo "### node2 bench_client"
    CORPCRON_RPC_AUTH_TOKEN="${AUTH_TOKEN}" ./build/bench_client "${HOST}" "${SERVER2_PORT}" "${CONCURRENCY}" "${REQUESTS}" "${MODE}"
    echo

    echo "## 3. 多节点锁竞争：同一任务只执行一次"
    insert_due_task "${TASK_ONCE}" "multinode-lock-once"
    if wait_history_count "${TASK_ONCE}" 1 30; then
        sleep 2
        once_count="$(history_count "${TASK_ONCE}")"
        once_nodes="$(history_nodes "${TASK_ONCE}")"
        echo "- task_id: ${TASK_ONCE}"
        echo "- history_count: ${once_count}"
        echo "- exec_nodes: ${once_nodes}"
        if [[ "${once_count}" == "1" ]]; then
            echo "- result: PASS，同一任务只产生一条执行历史。"
        else
            echo "- result: FAIL，出现重复执行历史。"
        fi
    else
        echo "- task_id: ${TASK_ONCE}"
        echo "- result: FAIL，30 秒内未观察到执行历史。"
    fi
    echo

    echo "## 4. 故障接管：停掉 node1 后 node2 执行新任务"
    if [[ "${USE_EXISTING_SERVERS}" != "1" ]] && [[ -n "${SERVER1_PID}" ]] && kill -0 "${SERVER1_PID}" 2>/dev/null; then
        kill "${SERVER1_PID}" 2>/dev/null || true
        wait "${SERVER1_PID}" 2>/dev/null || true
        echo "- node1 stopped: pid=${SERVER1_PID}"
    else
        echo "- node1 stop skipped: use_existing_servers=${USE_EXISTING_SERVERS}"
    fi
    sleep 3
    print_redis_snapshot
    insert_due_task "${TASK_FAILOVER}" "multinode-failover"
    if wait_history_count "${TASK_FAILOVER}" 1 30; then
        failover_count="$(history_count "${TASK_FAILOVER}")"
        failover_nodes="$(history_nodes "${TASK_FAILOVER}")"
        echo "- task_id: ${TASK_FAILOVER}"
        echo "- history_count: ${failover_count}"
        echo "- exec_nodes: ${failover_nodes}"
        if [[ "${failover_count}" -ge 1 ]]; then
            echo "- result: PASS，node1 停止后仍有节点执行任务。"
        else
            echo "- result: FAIL，未观察到接管执行。"
        fi
    else
        echo "- task_id: ${TASK_FAILOVER}"
        echo "- result: FAIL，30 秒内未观察到接管执行。"
    fi
    echo

    echo "## 5. 结束状态"
    print_process "server1 after failover" "${SERVER1_PID}"
    print_process "server2 after failover" "${SERVER2_PID}"
    print_http_snapshot "server1 after" "${METRICS1_URL}"
    print_http_snapshot "server2 after" "${METRICS2_URL}"

    echo "## 6. 原始日志"
    echo "- server1_log: ${SERVER1_LOG}"
    echo "- server2_log: ${SERVER2_LOG}"
} | tee "${RESULT_FILE}"

cp "${RESULT_FILE}" "${LATEST_FILE}"

echo
echo "saved_to=${RESULT_FILE}"
echo "latest=${LATEST_FILE}"
