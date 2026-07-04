#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
DOCKER="${DOCKER:-docker}"

RUN_INTEGRATION=0
START_COMPOSE=0
RUN_FORMAT=0
RUN_TIDY=0

usage() {
    cat <<EOF
Usage: scripts/check.sh [options]

Options:
  --integration   Run Redis/MySQL integration and e2e tests.
  --compose       Start docker compose dependencies before integration tests.
  --format        Run clang-format dry-run check when clang-format is available.
  --tidy          Configure CMake with clang-tidy when clang-tidy is available.
  -h, --help      Show this help.
EOF
}

has_cmd() {
    command -v "$1" >/dev/null 2>&1
}

wait_tcp() {
    local host="$1"
    local port="$2"
    local name="$3"
    local deadline=$((SECONDS + 60))
    while ((SECONDS < deadline)); do
        if timeout 1 bash -c "cat < /dev/null > /dev/tcp/${host}/${port}" >/dev/null 2>&1; then
            echo "OK   ${name} ${host}:${port}"
            return 0
        fi
        sleep 1
    done
    echo "FAIL ${name} ${host}:${port} is not ready" >&2
    return 1
}

while (($#)); do
    case "$1" in
        --integration) RUN_INTEGRATION=1 ;;
        --compose) START_COMPOSE=1 ;;
        --format) RUN_FORMAT=1 ;;
        --tidy) RUN_TIDY=1 ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

cd "${ROOT_DIR}"

if ((RUN_FORMAT)); then
    if has_cmd clang-format; then
        echo "== clang-format =="
        mapfile -t files < <(find include src tests tools client \
            \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
            -not -path '*/generated/*' | sort)
        if ((${#files[@]})); then
            clang-format --dry-run -Werror "${files[@]}"
        fi
    else
        echo "WARN clang-format is not installed; skip format check"
    fi
fi

if ((START_COMPOSE)); then
    echo "== docker compose up =="
    "${DOCKER}" compose up -d
    RUN_INTEGRATION=1
fi

if ((RUN_INTEGRATION)); then
    echo "== dependency readiness =="
    wait_tcp "${CORPCRON_REDIS_HOST:-127.0.0.1}" "${CORPCRON_REDIS_PORT:-6380}" "Redis"
    wait_tcp "${CORPCRON_MYSQL_HOST:-127.0.0.1}" "${CORPCRON_MYSQL_PORT:-3307}" "MySQL"
fi

echo "== configure =="
cmake_args=(-S . -B "${BUILD_DIR}")
if ((RUN_TIDY)); then
    if has_cmd clang-tidy; then
        cmake_args+=(-DCMAKE_CXX_CLANG_TIDY=clang-tidy)
    else
        echo "WARN clang-tidy is not installed; skip tidy configuration"
    fi
fi
cmake "${cmake_args[@]}"

echo "== build =="
cmake --build "${BUILD_DIR}" -j

echo "== tests =="
if ((RUN_INTEGRATION)); then
    CORPCRON_RUN_INTEGRATION_TESTS=1 ctest --test-dir "${BUILD_DIR}" --output-on-failure
else
    ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

echo "All checks passed."
