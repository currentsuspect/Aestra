#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKER_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_FILE="${WORKER_DIR}/wrangler.toml"
DB_NAME="${AESTRA_CF_D1_NAME:-aestra-license}"

cd "${WORKER_DIR}"

require_token() {
    if [ -z "${CLOUDFLARE_API_TOKEN:-}" ]; then
        echo "CLOUDFLARE_API_TOKEN is required for Cloudflare production operations." >&2
        exit 1
    fi
}

require_database_id() {
    if [ -z "${AESTRA_CF_D1_DATABASE_ID:-}" ]; then
        echo "AESTRA_CF_D1_DATABASE_ID is required. Run create-d1 or d1-list, then export the database id." >&2
        exit 1
    fi
}

check() {
    require_token
    npx wrangler whoami
    npx wrangler d1 list
}

create_d1() {
    require_token
    npx wrangler d1 create "${DB_NAME}"
    cat <<EOF_NEXT

Copy the database_id printed above into wrangler.toml, or run:

  export AESTRA_CF_D1_DATABASE_ID=<database-id>
  workers/license-signing/scripts/cf-prod.sh bind-d1

EOF_NEXT
}

bind_d1() {
    require_database_id
    tmp="$(mktemp)"
    awk -v db_name="${DB_NAME}" -v db_id="${AESTRA_CF_D1_DATABASE_ID}" '
      BEGIN { wrote = 0; skipping = 0 }
      /^# D1 production binding placeholder:/ { skipping = 1; next }
      skipping && /^# \[\[d1_databases\]\]/ {
        print "[[d1_databases]]"
        print "binding = \"AESTRA_LICENSE_DB\""
        print "database_name = \"" db_name "\""
        print "database_id = \"" db_id "\""
        print "migrations_dir = \"migrations\""
        wrote = 1
        next
      }
      skipping && /^# / { next }
      { skipping = 0; print }
      END {
        if (!wrote) {
          print ""
          print "[[d1_databases]]"
          print "binding = \"AESTRA_LICENSE_DB\""
          print "database_name = \"" db_name "\""
          print "database_id = \"" db_id "\""
          print "migrations_dir = \"migrations\""
        }
      }
    ' "${CONFIG_FILE}" > "${tmp}"
    mv "${tmp}" "${CONFIG_FILE}"
    echo "Bound ${DB_NAME} (${AESTRA_CF_D1_DATABASE_ID}) in ${CONFIG_FILE}"
}

migrate() {
    require_token
    npx wrangler d1 migrations apply AESTRA_LICENSE_DB --remote
}

put_secrets() {
    require_token
    cat <<'EOF_SECRETS'
Entering Cloudflare Worker secrets. Do not paste these into source files.

Required:
- AESTRA_LICENSE_SIGNING_PRIVATE_KEY: base64 raw 64-byte Ed25519 secret key
- AESTRA_ADMIN_API_KEY: admin/internal bearer secret
- AESTRA_SIGNING_KEY_ID: unsigned key id metadata
- AESTRA_STORAGE_MODE: d1

Optional for production login once mailer is real:
- AESTRA_LOGIN_MAILER_MODE
EOF_SECRETS
    npx wrangler secret put AESTRA_LICENSE_SIGNING_PRIVATE_KEY
    npx wrangler secret put AESTRA_ADMIN_API_KEY
    npx wrangler secret put AESTRA_SIGNING_KEY_ID
    npx wrangler secret put AESTRA_STORAGE_MODE
}

deploy() {
    require_token
    npm run typecheck
    npx wrangler deploy
}

case "${1:-help}" in
    check)
        check
        ;;
    create-d1)
        create_d1
        ;;
    bind-d1)
        bind_d1
        ;;
    migrate)
        migrate
        ;;
    put-secrets)
        put_secrets
        ;;
    deploy)
        deploy
        ;;
    *)
        cat <<EOF_USAGE
Usage: workers/license-signing/scripts/cf-prod.sh <check|create-d1|bind-d1|migrate|put-secrets|deploy>

Environment:
  CLOUDFLARE_API_TOKEN       Required for Cloudflare operations.
  AESTRA_CF_D1_NAME          Optional, defaults to ${DB_NAME}.
  AESTRA_CF_D1_DATABASE_ID   Required by bind-d1.

Typical production setup:
  export CLOUDFLARE_API_TOKEN=...
  workers/license-signing/scripts/cf-prod.sh check
  workers/license-signing/scripts/cf-prod.sh create-d1
  export AESTRA_CF_D1_DATABASE_ID=<database-id>
  workers/license-signing/scripts/cf-prod.sh bind-d1
  workers/license-signing/scripts/cf-prod.sh migrate
  workers/license-signing/scripts/cf-prod.sh put-secrets
  workers/license-signing/scripts/cf-prod.sh deploy
EOF_USAGE
        ;;
esac
