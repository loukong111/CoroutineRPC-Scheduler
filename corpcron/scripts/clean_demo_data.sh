#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

DOCKER="${DOCKER:-docker}"
MYSQL_CONTAINER="${MYSQL_CONTAINER:-corpcron-mysql}"
REDIS_CONTAINER="${REDIS_CONTAINER:-corpcron-redis}"
MYSQL_USER="${CORPCRON_MYSQL_USER:-corpcron}"
MYSQL_PASSWORD="${CORPCRON_MYSQL_PASSWORD:-corpcron_dev_password}"
MYSQL_DATABASE="${CORPCRON_MYSQL_DATABASE:-corpcron}"

echo "Cleaning MySQL demo data..."
"${DOCKER}" exec -i "${MYSQL_CONTAINER}" mysql -u"${MYSQL_USER}" -p"${MYSQL_PASSWORD}" "${MYSQL_DATABASE}" <<SQL
TRUNCATE TABLE task_history;
TRUNCATE TABLE tasks;
SQL

echo "Cleaning Redis demo keys..."
"${DOCKER}" exec -i "${REDIS_CONTAINER}" redis-cli FLUSHDB >/dev/null

echo "Demo data cleaned."
