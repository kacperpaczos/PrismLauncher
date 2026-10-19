#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
#
# SPDX-License-Identifier: GPL-3.0-only
#
# End to end smoke test of the headless mod management commands of the CLI
# spike. The test fabricates a minimal instance in a throwaway data directory
# and runs `mod list|search|info|install|disable|enable|update|remove`
# against the real Modrinth API.
#
# Usage (inside the build container):
#   tests/cli/spike-mod-smoke.sh
#
# Environment:
#   PRISM_BIN  path of the launcher binary (default: <repo>/build/prismlauncher)
#   KEEP_TMP   set to 1 to keep the temporary data directory

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRISM_BIN="${PRISM_BIN:-${REPO_ROOT}/build/prismlauncher}"

INSTANCE_ID="spike-test"
MC_VERSION="1.21.1"
FABRIC_LOADER="0.16.9"
MOD_SLUG="jei"

TMP_ROOT="$(mktemp -d -t prism-mod-smoke-XXXXXX)"
export HOME="${TMP_ROOT}/home"
export XDG_DATA_HOME="${HOME}/.local/share"
DATA_DIR="${XDG_DATA_HOME}/PrismLauncher"
INSTANCE_DIR="${DATA_DIR}/instances/${INSTANCE_ID}"
MODS_DIR="${INSTANCE_DIR}/minecraft/mods"
INDEX_DIR="${MODS_DIR}/.index"
LOG_FILE="${TMP_ROOT}/launcher.log"
JSON_FILE="${TMP_ROOT}/output.json"

cleanup() {
    if [ "${KEEP_TMP:-0}" = "1" ]; then
        echo "Temporary directory kept at ${TMP_ROOT}"
    else
        rm -rf "${TMP_ROOT}"
    fi
}
trap cleanup EXIT

fail() {
    echo "FAIL: $*" >&2
    if [ -f "${LOG_FILE}" ]; then
        echo "--- launcher log (tail) ---" >&2
        tail -n 40 "${LOG_FILE}" >&2 || true
    fi
    exit 1
}

step() {
    echo "== $*"
}

if [ ! -x "${PRISM_BIN}" ]; then
    fail "the launcher binary '${PRISM_BIN}' does not exist - build it first"
fi

# --------------------------------------------------------------------------
# instance fabrication
# --------------------------------------------------------------------------
create_instance() {
    mkdir -p "${MODS_DIR}"

    cat > "${INSTANCE_DIR}/instance.cfg" <<EOF
[Instance]
InstanceType=OneSix
name=${INSTANCE_ID}
iconKey=default
uuid=11111111-2222-3333-4444-555555555555
EOF

    # The component file of the instance. The `cachedVersion` of a component
    # is the version the launcher resolved for it - without it the Minecraft
    # version of the instance is unknown.
    cat > "${INSTANCE_DIR}/mmc-pack.json" <<EOF
{
  "formatVersion": 1,
  "components": [
    { "uid": "net.minecraft", "version": "${MC_VERSION}", "cachedVersion": "${MC_VERSION}" },
    { "uid": "net.fabricmc.fabric-loader", "version": "${FABRIC_LOADER}", "cachedVersion": "${FABRIC_LOADER}" }
  ]
}
EOF
}

# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------
LAST_OUT=""
LAST_RC=0

run_cmd() {
    local out rc
    set +e
    out="$(QT_QPA_PLATFORM=offscreen "${PRISM_BIN}" "$@" 2>>"${LOG_FILE}")"
    rc=$?
    set -e
    LAST_OUT="${out}"
    LAST_RC=${rc}
    echo "   \$ prismlauncher $* -> exit ${rc}"
}

expect_rc() {
    local expected="$1"
    if [ "${LAST_RC}" != "${expected}" ]; then
        echo "   output: ${LAST_OUT}" >&2
        fail "expected exit code ${expected}, got ${LAST_RC}"
    fi
}

# check_json <python expression> - the expression is evaluated with the parsed
# JSON document bound to `d`.
check_json() {
    local expr="$1"
    printf '%s' "${LAST_OUT}" > "${JSON_FILE}"
    if ! python3 - "${JSON_FILE}" "${expr}" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    raw = handle.read()
expr = sys.argv[2]
try:
    d = json.loads(raw)
except Exception as exc:  # noqa: BLE001
    print("FAIL: the output is not valid JSON: %r (%s)" % (raw, exc))
    sys.exit(1)
try:
    ok = eval(expr, {"d": d, "len": len, "any": any, "all": all})  # noqa: S307
except Exception as exc:  # noqa: BLE001
    print("FAIL: could not evaluate %r: %s" % (expr, exc))
    sys.exit(1)
if not ok:
    print("FAIL: the assertion %r failed" % expr)
    print("output: %s" % raw)
    sys.exit(1)
PY
    then
        exit 1
    fi
}

expect_file() {
    if [ ! -e "$1" ]; then
        fail "the file '$1' does not exist"
    fi
}

expect_no_file() {
    if [ -e "$1" ]; then
        fail "the file '$1' still exists"
    fi
}

# find_mod_file <extended regexp> - the name of the first mod file that
# matches the pattern.
find_mod_file() {
    ls "${MODS_DIR}" 2>/dev/null | grep -E -i "$1" | head -n 1 || true
}

expect_mod_file() {
    local match
    match="$(find_mod_file "$1")"
    if [ -z "${match}" ]; then
        fail "no mod file matching '$1' was found in ${MODS_DIR}"
    fi
}

expect_no_mod_file() {
    local match
    match="$(find_mod_file "$1")"
    if [ -n "${match}" ]; then
        fail "the mod file '${match}' still exists in ${MODS_DIR}"
    fi
}

# --------------------------------------------------------------------------

create_instance

step "mod list (empty instance)"
run_cmd mod list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["instance"] == "spike-test"'
check_json 'd["total"] == 0'
check_json 'd["mods"] == []'

step "mod search"
run_cmd mod search "${MOD_SLUG}" --mc "${MC_VERSION}" --loader fabric --limit 3
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["offset"] == 0'
check_json 'd["limit"] == 3'
check_json 'd["total"] > 0'
check_json 'len(d["results"]) == 3'
check_json 'all(r["slug"] for r in d["results"])'
check_json 'any("jei" in r["slug"] for r in d["results"])'

step "mod info (before the install)"
run_cmd mod info "${MOD_SLUG}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["project"]["slug"] == "jei"'
check_json 'len(d["versions"]) > 0'
check_json 'd["installed"] is None'

step "mod install"
run_cmd mod install "${MOD_SLUG}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["installed"]) == 1'
check_json 'd["installed"][0]["slug"] == "jei"'
check_json 'd["installed"][0]["source"] == "modrinth"'
check_json 'd["failed"] == []'
expect_mod_file '^jei.*\.jar$'
expect_file "${INDEX_DIR}/jei.pw.toml"

step "mod list (with the installed mod)"
run_cmd mod list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] >= 1'
check_json 'any(m["slug"] == "jei" and m["source"] == "modrinth" for m in d["mods"])'
check_json 'all(m["enabled"] for m in d["mods"])'

step "mod disable"
run_cmd mod disable "${MOD_SLUG}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["changed"]) == 1'
check_json 'd["changed"][0]["slug"] == "jei"'
check_json 'd["changed"][0]["enabled"] is False'
check_json 'd["failed"] == []'
expect_mod_file '^jei.*\.jar\.disabled$' 

step "mod list (the disabled mod is hidden by default)"
run_cmd mod list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'all(m["slug"] != "jei" for m in d["mods"])'

step "mod list --include-disabled"
run_cmd mod list -i "${INSTANCE_ID}" --include-disabled
expect_rc 0
check_json 'any(m["slug"] == "jei" and m["enabled"] is False for m in d["mods"])'

step "mod enable"
run_cmd mod enable "${MOD_SLUG}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["changed"]) == 1'
check_json 'd["changed"][0]["slug"] == "jei"'
check_json 'd["changed"][0]["enabled"] is True'
check_json 'd["failed"] == []'
expect_no_mod_file '^jei.*\.jar\.disabled$' 

step "mod update --all --dry-run"
run_cmd mod update --all -i "${INSTANCE_ID}" --dry-run
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["dry_run"] is True'
check_json 'd["failed"] == []'
check_json 'isinstance(d["updated"], list)'
check_json 'isinstance(d["up_to_date"], list)'
check_json 'isinstance(d["skipped"], list)'

step "mod update (the installed version is the newest one)"
run_cmd mod update --all -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["dry_run"] is False'
check_json 'd["failed"] == []'

step "mod info (the installed mod)"
run_cmd mod info "${MOD_SLUG}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["project"]["slug"] == "jei"'
check_json 'd["installed"] is not None'
check_json 'd["installed"]["slug"] == "jei"'
check_json 'd["installed"]["source"] == "modrinth"'

step "mod remove"
run_cmd mod remove "${MOD_SLUG}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["removed"]) == 1'
check_json 'd["removed"][0]["slug"] == "jei"'
check_json 'd["failed"] == []'
expect_no_mod_file '^jei.*\.jar$'
expect_no_file "${INDEX_DIR}/jei.pw.toml"

step "mod list (the mod is gone)"
run_cmd mod list -i "${INSTANCE_ID}" --include-disabled
expect_rc 0
check_json 'all(m["slug"] != "jei" for m in d["mods"])'

step "mod remove (a target that does not exist)"
run_cmd mod remove does-not-exist -i "${INSTANCE_ID}"
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "not_found"'

echo
echo "All smoke test steps passed."
