#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKER_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${WORKER_DIR}/../.." && pwd)"

PORT="${AESTRA_SMOKE_PORT:-8787}"
EMAIL="${AESTRA_SMOKE_EMAIL:-smoke+aestra@example.test}"
ADMIN_KEY="${AESTRA_SMOKE_ADMIN_KEY:-local-smoke-admin}"
TIER="${AESTRA_SMOKE_TIER:-founder}"
PERSIST_DIR="${AESTRA_SMOKE_D1_STATE:-${WORKER_DIR}/.wrangler/local-smoke-state}"
CONFIG_FILE="${WORKER_DIR}/.wrangler.local-smoke.toml"
ENV_FILE="${WORKER_DIR}/.dev.vars.local-smoke"

cd "${WORKER_DIR}"

dev_secret() {
    node --input-type=module -e '
import nacl from "tweetnacl";
const seed = Uint8Array.from(Array.from({ length: 32 }, (_, i) => i));
const keyPair = nacl.sign.keyPair.fromSeed(seed);
let binary = "";
for (const byte of keyPair.secretKey) binary += String.fromCharCode(byte);
console.log(Buffer.from(binary, "binary").toString("base64"));
'
}

write_local_files() {
    if [ ! -d "${WORKER_DIR}/node_modules/tweetnacl" ]; then
        echo "Missing Worker dependencies. Run npm install in workers/license-signing first." >&2
        exit 1
    fi

    mkdir -p "${PERSIST_DIR}"
    cat > "${CONFIG_FILE}" <<EOF_CONFIG
name = "aestra-license-signing-local-smoke"
main = "src/index.ts"
compatibility_date = "2026-05-06"

[[d1_databases]]
binding = "AESTRA_LICENSE_DB"
database_name = "aestra-license-local-smoke"
database_id = "00000000-0000-0000-0000-000000000000"
migrations_dir = "migrations"
EOF_CONFIG

    cat > "${ENV_FILE}" <<EOF_ENV
AESTRA_STORAGE_MODE=d1
AESTRA_LOGIN_MAILER_MODE=fixture
AESTRA_ADMIN_API_KEY=${ADMIN_KEY}
AESTRA_SIGNING_KEY_ID=AESTRA_DEV_TEST_PUBKEY_V1
AESTRA_LICENSE_SIGNING_PRIVATE_KEY=$(dev_secret)
EOF_ENV
    chmod 600 "${ENV_FILE}"
}

prepare() {
    write_local_files
    CI=1 npx wrangler d1 migrations apply AESTRA_LICENSE_DB --local --persist-to "${PERSIST_DIR}" --config "${CONFIG_FILE}"
    echo "Prepared local D1 smoke database at ${PERSIST_DIR}"
}

run_worker() {
    prepare
    exec npx wrangler dev --local --persist-to "${PERSIST_DIR}" --config "${CONFIG_FILE}" --env-file "${ENV_FILE}" --port "${PORT}"
}

run_native_smoke() {
    cd "${REPO_ROOT}"
    AESTRA_ACCOUNT_API_BASE_URL="http://127.0.0.1:${PORT}" \
    AESTRA_SMOKE_EMAIL="${EMAIL}" \
    AESTRA_SMOKE_ADMIN_KEY="${ADMIN_KEY}" \
    AESTRA_SMOKE_TIER="${TIER}" \
    AESTRA_SMOKE_EXPECT_LIVE_WORKER=1 \
        ctest --test-dir build-release -R "AccountEndToEndSmoke" --output-on-failure
}

print_env() {
    cat <<EOF_EXPORTS
export AESTRA_ACCOUNT_API_BASE_URL=http://127.0.0.1:${PORT}
export AESTRA_SMOKE_EMAIL=${EMAIL}
export AESTRA_SMOKE_ADMIN_KEY=${ADMIN_KEY}
export AESTRA_SMOKE_TIER=${TIER}
export AESTRA_SMOKE_EXPECT_LIVE_WORKER=1
EOF_EXPORTS
}

case "${1:-help}" in
    prepare)
        prepare
        ;;
    worker)
        run_worker
        ;;
    native)
        run_native_smoke
        ;;
    env)
        print_env
        ;;
    *)
        cat <<EOF_USAGE
Usage: workers/license-signing/scripts/dev-smoke.sh <prepare|worker|native|env>

prepare  Generate ignored local dev vars/config and apply local D1 migrations.
worker   Prepare local D1, then run wrangler dev on port ${PORT}.
native   Run SecAccountEndToEndSmoke against the Worker already running on port ${PORT}.
env      Print the native smoke environment exports.

The generated .dev.vars.local-smoke file contains dev/test-only signing material
derived from the Worker cross-language fixture seed. Do not commit it.
EOF_USAGE
        ;;
esac
