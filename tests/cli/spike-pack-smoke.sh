#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
#
# SPDX-License-Identifier: GPL-3.0-only
#
# End to end smoke test of the headless resource, shader and texture pack
# commands of the CLI spike. The test fabricates a minimal instance in a
# throwaway data directory and runs
# `resource|shader|texture list|search|info|install|disable|enable|remove`
# against the real Modrinth API.
#
# Usage (inside the build container):
#   tests/cli/spike-pack-smoke.sh
#
# Environment:
#   PRISM_BIN  path of the launcher binary (default: <repo>/build/prismlauncher)
#   KEEP_TMP   set to 1 to keep the temporary data directory

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRISM_BIN="${PRISM_BIN:-${REPO_ROOT}/build/prismlauncher}"

INSTANCE_ID="spike-pack-test"
MC_VERSION="1.21.1"

# A resource pack, a shader pack and a texture pack of Modrinth. The texture
# pack is published as a resource pack - Modrinth does not have a project type
# for texture packs.
RESOURCE_PACK="fresh-animations"
SHADER_PACK="complementary-reimagined"
TEXTURE_PACK="cartoon-texture-pack"

TMP_ROOT="$(mktemp -d -t prism-pack-smoke-XXXXXX)"
export HOME="${TMP_ROOT}/home"
export XDG_DATA_HOME="${HOME}/.local/share"
DATA_DIR="${XDG_DATA_HOME}/PrismLauncher"
INSTANCE_DIR="${DATA_DIR}/instances/${INSTANCE_ID}"
GAME_DIR="${INSTANCE_DIR}/minecraft"
RESOURCE_DIR="${GAME_DIR}/resourcepacks"
SHADER_DIR="${GAME_DIR}/shaderpacks"
TEXTURE_DIR="${GAME_DIR}/texturepacks"
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
    mkdir -p "${RESOURCE_DIR}" "${SHADER_DIR}" "${TEXTURE_DIR}"

    cat > "${INSTANCE_DIR}/instance.cfg" <<EOF
[Instance]
InstanceType=OneSix
name=${INSTANCE_ID}
iconKey=default
uuid=22222222-3333-4444-5555-666666666666
EOF

    # The component file of the instance. The `cachedVersion` of a component
    # is the version the launcher resolved for it - without it the Minecraft
    # version of the instance is unknown.
    cat > "${INSTANCE_DIR}/mmc-pack.json" <<EOF
{
  "formatVersion": 1,
  "components": [
    { "uid": "net.minecraft", "version": "${MC_VERSION}", "cachedVersion": "${MC_VERSION}" }
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
    LAST_RC="${rc}"
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
except Exception as exc:
    print("FAIL: the output is not valid JSON: %r (%s)" % (raw, exc))
    sys.exit(1)
try:
    ok = eval(expr, {"d": d, "len": len, "any": any, "all": all})
except Exception as exc:
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

# json_value <python expression> - the value is read from the last output.
json_value() {
    python3 - "${JSON_FILE}" "$1" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    d = json.load(handle)
print(eval(sys.argv[2], {"d": d}))
PY
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

# find_pack_file <directory> <extended regexp> - the name of the first file of
# the directory that matches the pattern.
find_pack_file() {
    ls "$1" 2>/dev/null | grep -E -i "$2" | head -n 1 || true
}

expect_pack_file() {
    local dir="$1" match
    match="$(find_pack_file "${dir}" "$2")"
    if [ -z "${match}" ]; then
        fail "no pack file matching '$2' was found in ${dir}"
    fi
    echo "${match}"
}

expect_no_pack_file() {
    local dir="$1" match
    match="$(find_pack_file "${dir}" "$2")"
    if [ -n "${match}" ]; then
        fail "the pack file '${match}' still exists in ${dir}"
    fi
}

# --------------------------------------------------------------------------

create_instance

step "resource list (empty instance)"
run_cmd resource list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["instance"] == "spike-pack-test"'
check_json 'd["total"] == 0'
check_json 'd["resource_packs"] == []'

step "resource search"
run_cmd resource search fresh --limit 3
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["offset"] == 0'
check_json 'd["limit"] == 3'
check_json 'd["total"] > 0'
check_json 'len(d["results"]) == 3'
check_json 'all(r["slug"] for r in d["results"])'
check_json 'any(r["slug"] == "fresh-animations" for r in d["results"])'

step "resource search (with a Minecraft version filter)"
run_cmd resource search fresh --mc "${MC_VERSION}" --limit 2
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["results"]) == 2'

step "resource info (before the install)"
run_cmd resource info "${RESOURCE_PACK}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["project"]["slug"] == "fresh-animations"'
check_json 'len(d["versions"]) > 0'
check_json 'd["installed"] is None'

step "resource install"
run_cmd resource install "${RESOURCE_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["installed"]) == 1'
check_json 'd["installed"][0]["slug"] == "fresh-animations"'
check_json 'd["installed"][0]["source"] == "modrinth"'
check_json 'd["failed"] == []'
RESOURCE_FILE="$(expect_pack_file "${RESOURCE_DIR}" '^.*\.zip$')"
expect_file "${RESOURCE_DIR}/.index/fresh-animations.pw.toml"

step "resource list (with the installed pack)"
run_cmd resource list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] >= 1'
check_json 'any(p["slug"] == "fresh-animations" and p["source"] == "modrinth" for p in d["resource_packs"])'
check_json 'all(p["enabled"] for p in d["resource_packs"])'
# the pack report has no lock_update, but it has the pack format
check_json 'all("lock_update" not in p for p in d["resource_packs"])'
check_json 'any(p["pack_format"] is not None for p in d["resource_packs"])'

step "resource disable"
run_cmd resource disable "${RESOURCE_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["changed"]) == 1'
check_json 'd["changed"][0]["slug"] == "fresh-animations"'
check_json 'd["changed"][0]["enabled"] is False'
check_json 'd["failed"] == []'
expect_pack_file "${RESOURCE_DIR}" '^.*\.zip\.disabled$'

step "resource list (the disabled pack is hidden by default)"
run_cmd resource list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'all(p["slug"] != "fresh-animations" for p in d["resource_packs"])'

step "resource list --include-disabled"
run_cmd resource list -i "${INSTANCE_ID}" --include-disabled
expect_rc 0
check_json 'any(p["slug"] == "fresh-animations" and p["enabled"] is False for p in d["resource_packs"])'

step "resource enable"
run_cmd resource enable "${RESOURCE_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["changed"]) == 1'
check_json 'd["changed"][0]["slug"] == "fresh-animations"'
check_json 'd["changed"][0]["enabled"] is True'
check_json 'd["failed"] == []'
expect_no_pack_file "${RESOURCE_DIR}" '^.*\.zip\.disabled$'

step "resource info (the installed pack)"
run_cmd resource info "${RESOURCE_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["project"]["slug"] == "fresh-animations"'
check_json 'd["installed"] is not None'
check_json 'd["installed"]["slug"] == "fresh-animations"'
check_json 'd["installed"]["source"] == "modrinth"'

step "resource remove"
run_cmd resource remove "${RESOURCE_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["removed"]) == 1'
check_json 'd["removed"][0]["slug"] == "fresh-animations"'
check_json 'd["failed"] == []'
expect_no_pack_file "${RESOURCE_DIR}" '^.*\.zip$'
expect_no_file "${RESOURCE_DIR}/.index/fresh-animations.pw.toml"

step "resource list (the pack is gone)"
run_cmd resource list -i "${INSTANCE_ID}" --include-disabled
expect_rc 0
check_json 'all(p["slug"] != "fresh-animations" for p in d["resource_packs"])'

step "resource remove (a target that does not exist)"
run_cmd resource remove does-not-exist -i "${INSTANCE_ID}"
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "not_found"'

step "resource list (an instance that does not exist)"
run_cmd resource list -i does-not-exist
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "instance_not_found"'

step "shader list (empty instance)"
run_cmd shader list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] == 0'
check_json 'd["shader_packs"] == []'

step "shader search"
run_cmd shader search complementary --limit 3
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] > 0'
check_json 'len(d["results"]) == 3'
check_json 'any(r["slug"] == "complementary-reimagined" for r in d["results"])'

step "shader install"
run_cmd shader install "${SHADER_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["installed"]) == 1'
check_json 'd["installed"][0]["slug"] == "complementary-reimagined"'
check_json 'd["installed"][0]["source"] == "modrinth"'
check_json 'd["failed"] == []'
expect_pack_file "${SHADER_DIR}" '^.*\.zip$'

step "shader list (with the installed pack)"
run_cmd shader list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] >= 1'
check_json 'any(p["slug"] == "complementary-reimagined" and p["source"] == "modrinth" for p in d["shader_packs"])'

step "shader disable and enable"
run_cmd shader disable "${SHADER_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["changed"]) == 1'
check_json 'd["changed"][0]["enabled"] is False'
expect_pack_file "${SHADER_DIR}" '^.*\.zip\.disabled$'
run_cmd shader enable "${SHADER_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["changed"]) == 1'
check_json 'd["changed"][0]["enabled"] is True'
expect_no_pack_file "${SHADER_DIR}" '^.*\.zip\.disabled$'

step "shader remove"
run_cmd shader remove "${SHADER_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["removed"]) == 1'
expect_no_pack_file "${SHADER_DIR}" '^.*\.zip$'

step "texture list (empty instance)"
run_cmd texture list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] == 0'
check_json 'd["texture_packs"] == []'

step "texture search"
run_cmd texture search texture --limit 3
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] > 0'
check_json 'len(d["results"]) == 3'

step "texture install"
run_cmd texture install "${TEXTURE_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["installed"]) == 1'
check_json 'd["installed"][0]["slug"] == "cartoon-texture-pack"'
check_json 'd["installed"][0]["source"] == "modrinth"'
check_json 'd["failed"] == []'
expect_pack_file "${TEXTURE_DIR}" '^.*\.zip$'

step "texture list (with the installed pack)"
run_cmd texture list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] >= 1'
check_json 'any(p["slug"] == "cartoon-texture-pack" and p["source"] == "modrinth" for p in d["texture_packs"])'

step "texture remove"
run_cmd texture remove "${TEXTURE_PACK}" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["removed"]) == 1'
expect_no_pack_file "${TEXTURE_DIR}" '^.*\.zip$'

echo
echo "All smoke test steps passed."
