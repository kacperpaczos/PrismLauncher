#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
#
# SPDX-License-Identifier: GPL-3.0-only
#
# End to end smoke test of the headless instance, settings and version
# commands of the CLI spike. The test fabricates a minimal instance in a
# throwaway data directory and runs
# `instance list|info|create|duplicate|rename|delete|icon`,
# `settings list|get|set|unset` and
# `version show|list-available|change-mc|install-loader|remove-loader`
# against it. The commands that need the network use the real metadata of
# Mojang and of the mod loaders.
#
# Usage (inside the build container):
#   tests/cli/spike-instance-smoke.sh
#
# Environment:
#   PRISM_BIN  path of the launcher binary (default: <repo>/build/prismlauncher)
#   KEEP_TMP   set to 1 to keep the temporary data directory
#   MC_VERSION the Minecraft version the test uses (default: 1.21.1)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRISM_BIN="${PRISM_BIN:-${REPO_ROOT}/build/prismlauncher}"

INSTANCE_ID="spike-test"
MC_VERSION="${MC_VERSION:-1.21.1}"
FABRIC_LOADER="0.16.9"

TMP_ROOT="$(mktemp -d -t prism-instance-smoke-XXXXXX)"
export HOME="${TMP_ROOT}/home"
export XDG_DATA_HOME="${HOME}/.local/share"
DATA_DIR="${XDG_DATA_HOME}/PrismLauncher"
INSTANCE_DIR="${DATA_DIR}/instances/${INSTANCE_ID}"
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
    mkdir -p "${INSTANCE_DIR}/minecraft/mods"

    cat > "${INSTANCE_DIR}/instance.cfg" <<EOF
[General]
ConfigVersion=1.3
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
    { "uid": "net.minecraft", "version": "${MC_VERSION}", "cachedVersion": "${MC_VERSION}", "important": true }
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

# run_cmd_in <directory> <arguments> - the command is run in the given
# directory, so that a relative path of the client can be tested.
run_cmd_in() {
    local dir="$1"
    shift
    local out rc
    set +e
    out="$(cd "${dir}" && QT_QPA_PLATFORM=offscreen "${PRISM_BIN}" "$@" 2>>"${LOG_FILE}")"
    rc=$?
    set -e
    LAST_OUT="${out}"
    LAST_RC="${rc}"
    echo "   \$ (cd ${dir} && prismlauncher $*) -> exit ${rc}"
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
try:
    print(eval(sys.argv[2], {"d": d}))
except Exception:
    print("")
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

# --------------------------------------------------------------------------

create_instance

step "instance list"
run_cmd instance list
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] == 1'
check_json 'd["instances"][0]["id"] == "spike-test"'
check_json "d[\"instances\"][0][\"mc_version\"] == \"${MC_VERSION}\""
# the running state of an instance lives in the process of the launcher
check_json 'd["instances"][0]["running"] is None'

step "instance info"
run_cmd instance info "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["instance"]["id"] == "spike-test"'
check_json 'len(d["instance"]["components"]) >= 1'
check_json 'any(c["uid"] == "net.minecraft" for c in d["instance"]["components"])'
check_json 'd["instance"]["game_root"].endswith("minecraft")'

step "instance info (an instance that does not exist)"
run_cmd instance info does-not-exist
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "instance_not_found"'

step "settings list"
run_cmd settings list "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["instance"] == "spike-test"'
check_json 'd["total"] > 0'
# every setting has the fields of the schema
check_json 'all(set(k) == {"key", "value", "type", "source", "overridden", "ini_key", "gate"} for k in d["settings"])'
check_json 'any(s["key"] == "memory.max" and s["ini_key"] == "MaxMemAlloc" and s["gate"] == "OverrideMemory" for s in d["settings"])'
# the instance does not override anything yet
check_json 'all(not s["overridden"] for s in d["settings"] if s["gate"])'

step "settings get"
run_cmd settings get "${INSTANCE_ID}" memory.max memory.min
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] == 2'
check_json 'd["settings"][0]["key"] == "memory.max"'
check_json 'd["settings"][0]["type"] == "int"'
check_json 'd["settings"][0]["source"] == "default"'

step "settings get (a key that does not exist)"
run_cmd settings get "${INSTANCE_ID}" nosuchkey
expect_rc 2
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "unknown_setting"'

step "settings set"
run_cmd settings set "${INSTANCE_ID}" memory.max=8192 memory.min=1024
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["settings"][0]["value"] == 8192'
check_json 'd["settings"][0]["overridden"] is True'
check_json 'd["settings"][0]["source"] == "instance"'
# the value is written to the file of the instance, together with the flag of
# the group
expect_file "${INSTANCE_DIR}/instance.cfg"
grep -q "^MaxMemAlloc=8192$" "${INSTANCE_DIR}/instance.cfg" || fail "the memory was not written to instance.cfg"
grep -q "^MinMemAlloc=1024$" "${INSTANCE_DIR}/instance.cfg" || fail "the memory was not written to instance.cfg"
grep -q "^OverrideMemory=true$" "${INSTANCE_DIR}/instance.cfg" || fail "the flag of the group was not written to instance.cfg"

step "settings get (the values of the instance)"
run_cmd settings get "${INSTANCE_ID}" memory.max memory.min
expect_rc 0
check_json 'd["settings"][0]["value"] == 8192'
check_json 'd["settings"][0]["overridden"] is True'
check_json 'd["settings"][1]["value"] == 1024'

step "settings set (the minimum memory is bigger than the maximum memory)"
run_cmd settings set "${INSTANCE_ID}" memory.min=99999
expect_rc 2
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "invalid_memory"'

step "settings set (a value that is not a number)"
run_cmd settings set "${INSTANCE_ID}" memory.max=notanumber
expect_rc 2
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "invalid_value"'

step "settings set (an environment variable)"
run_cmd settings set "${INSTANCE_ID}" env.SPIKE_TEST=hello
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["settings"][0]["key"] == "env.SPIKE_TEST"'
check_json 'd["settings"][0]["value"] == "hello"'

step "settings get (the environment variable)"
run_cmd settings get "${INSTANCE_ID}" env.SPIKE_TEST
expect_rc 0
check_json 'd["settings"][0]["value"] == "hello"'

step "settings list --modified"
run_cmd settings list "${INSTANCE_ID}" --modified
expect_rc 0
check_json 'd["ok"] is True'
check_json 'all(s["overridden"] for s in d["settings"])'
check_json 'any(s["key"] == "memory.max" for s in d["settings"])'

step "settings unset"
run_cmd settings unset "${INSTANCE_ID}" memory.max
expect_rc 0
check_json 'd["ok"] is True'
run_cmd settings get "${INSTANCE_ID}" memory.max
expect_rc 0
check_json 'd["settings"][0]["overridden"] is False'
check_json 'd["settings"][0]["source"] == "default"'

step "version show"
run_cmd version show "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["instance"] == "spike-test"'
check_json "d[\"mc_version\"] == \"${MC_VERSION}\""
check_json 'any(c["uid"] == "net.minecraft" and c["important"] is True for c in d["components"])'
check_json 'all("uid" in c and "version" in c and "enabled" in c and "removable" in c for c in d["components"])'

step "version list-available (the versions of Minecraft)"
run_cmd version list-available --type release
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["uid"] == "net.minecraft"'
check_json 'd["total"] > 0'
check_json "any(v[\"version\"] == \"${MC_VERSION}\" for v in d[\"versions\"])"
check_json 'all(v["type"] == "release" for v in d["versions"])'

step "version list-available (the versions of a mod loader)"
run_cmd version list-available --loader fabric --mc-version "${MC_VERSION}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["loader"] == "fabric"'
check_json 'd["uid"] == "net.fabricmc.fabric-loader"'
check_json "any(v[\"version\"] == \"${FABRIC_LOADER}\" for v in d[\"versions\"])"

step "version install-loader (the fabric loader)"
run_cmd version install-loader "${INSTANCE_ID}" fabric "${FABRIC_LOADER}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["loader"] == "fabric"'
check_json "d[\"loader_version\"] == \"${FABRIC_LOADER}\""
check_json 'd["conflicts"] == []'

step "version show (the fabric loader is a component now)"
run_cmd version show "${INSTANCE_ID}"
expect_rc 0
check_json 'd["loader"] == "fabric"'
check_json "d[\"loader_version\"] == \"${FABRIC_LOADER}\""
check_json 'any(c["uid"] == "net.fabricmc.fabric-loader" for c in d["components"])'

step "version install-loader (a version that does not exist)"
run_cmd version install-loader "${INSTANCE_ID}" fabric 0.0.0-does-not-exist
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "version_not_found"'

step "version install-loader (a loader that conflicts with the fabric loader)"
run_cmd version install-loader "${INSTANCE_ID}" quilt latest
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["loader"] == "quilt"'
# the fabric loader is disabled by default
check_json 'any(c["uid"] == "net.fabricmc.fabric-loader" and c["action"] == "disabled" for c in d["conflicts"])'
run_cmd version show "${INSTANCE_ID}"
expect_rc 0
check_json 'any(c["uid"] == "net.fabricmc.fabric-loader" and c["enabled"] is False for c in d["components"])'

step "version remove-loader (the quilt loader)"
run_cmd version remove-loader "${INSTANCE_ID}" quilt
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["removed"] is True'
run_cmd version show "${INSTANCE_ID}"
expect_rc 0
check_json 'all(c["uid"] != "org.quiltmc.quilt-loader" for c in d["components"])'

step "version remove-loader (a loader the instance does not use)"
run_cmd version remove-loader "${INSTANCE_ID}" quilt
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "loader_not_found"'

step "version change-mc"
run_cmd version change-mc "${INSTANCE_ID}" 1.20.4
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["mc_version"] == "1.20.4"'
run_cmd version show "${INSTANCE_ID}"
expect_rc 0
check_json 'd["mc_version"] == "1.20.4"'
check_json 'any(c["uid"] == "net.minecraft" and c["version"] == "1.20.4" for c in d["components"])'

step "version change-mc (a version that does not exist)"
run_cmd version change-mc "${INSTANCE_ID}" does-not-exist
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "version_not_found"'

step "instance duplicate"
run_cmd instance duplicate "${INSTANCE_ID}" --name "spike-copy"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["id"] == "spike-copy"'
check_json 'd["source"] == "spike-test"'
check_json 'd["copy_mode"] == "copy"'
expect_file "${DATA_DIR}/instances/spike-copy/instance.cfg"

step "instance list (the copy is there)"
run_cmd instance list
expect_rc 0
check_json 'd["total"] == 2'
check_json 'any(i["id"] == "spike-copy" for i in d["instances"])'

step "instance rename"
run_cmd instance rename spike-copy "spike-renamed"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["old_id"] == "spike-copy"'
check_json 'd["new_id"] == "spike-renamed"'
check_json 'd["folder_renamed"] is True'
expect_file "${DATA_DIR}/instances/spike-renamed/instance.cfg"
expect_no_file "${DATA_DIR}/instances/spike-copy/instance.cfg"

step "instance rename (the folder is kept)"
run_cmd instance rename spike-renamed "spike-renamed-2" --keep-folder
expect_rc 0
check_json 'd["old_id"] == "spike-renamed"'
check_json 'd["new_id"] == "spike-renamed"'
check_json 'd["folder_renamed"] is False'
expect_file "${DATA_DIR}/instances/spike-renamed/instance.cfg"

step "instance icon set (an icon key)"
run_cmd instance icon set "${INSTANCE_ID}" default
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["icon_key"] == "default"'

step "instance icon set (an icon that does not exist)"
run_cmd instance icon set "${INSTANCE_ID}" no-such-icon
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "icon_not_found"'

step "instance icon set (an image file)"
printf 'not a real image\n' > "${TMP_ROOT}/spike-icon.png"
run_cmd instance icon set "${INSTANCE_ID}" "${TMP_ROOT}/spike-icon.png"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["icon_key"] == "spike-icon"'
expect_file "${DATA_DIR}/icons/spike-icon.png"

# The icon of the launcher itself must not be deleted by the installation of
# the icon.
step "instance icon set (the icon of the launcher itself)"
run_cmd instance icon set "${INSTANCE_ID}" "${DATA_DIR}/icons/spike-icon.png"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["icon_key"] == "spike-icon"'
expect_file "${DATA_DIR}/icons/spike-icon.png"

step "instance delete"
run_cmd instance delete spike-renamed --purge
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["purged"] is True'
expect_no_file "${DATA_DIR}/instances/spike-renamed/instance.cfg"

step "instance list (the copy is gone)"
run_cmd instance list
expect_rc 0
check_json 'd["total"] == 1'
check_json 'd["instances"][0]["id"] == "spike-test"'

step "instance delete (the instance does not exist)"
run_cmd instance delete does-not-exist
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "instance_not_found"'

step "instance create (a vanilla instance with a mod loader)"
run_cmd instance create --name spike-created --mc "${MC_VERSION}" --loader fabric --loader-version "${FABRIC_LOADER}" --no-download
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["id"] == "spike-created"'
check_json "d[\"mc_version\"] == \"${MC_VERSION}\""
check_json 'd["loader"] == "fabric"'
check_json 'd["downloaded"] is False'
expect_file "${DATA_DIR}/instances/spike-created/instance.cfg"

step "instance list (the new instance is there)"
run_cmd instance list
expect_rc 0
check_json 'd["total"] == 2'
check_json 'any(i["id"] == "spike-created" for i in d["instances"])'

step "instance create (a Minecraft version that does not exist)"
run_cmd instance create --name spike-nope --mc does-not-exist --no-download
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "version_not_found"'

step "instance create (a name and a version are needed)"
run_cmd instance create --name spike-nope
expect_rc 2
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "parse_error"'

step "instance delete (the created instance)"
run_cmd instance delete spike-created --purge
expect_rc 0
check_json 'd["ok"] is True'

# The import of a pack must not ask anything - a headless run has nobody to
# answer the dialogs of the launcher.
step "instance create --from (the import of a pack)"
mkdir -p "${TMP_ROOT}/pack"
cat > "${TMP_ROOT}/pack/instance.cfg" <<EOF
[General]
ConfigVersion=1.3
InstanceType=OneSix
name=Imported Pack
iconKey=default
uuid=33333333-4444-5555-6666-777777777777
EOF
cat > "${TMP_ROOT}/pack/mmc-pack.json" <<EOF
{
  "formatVersion": 1,
  "components": [
    { "uid": "net.minecraft", "version": "${MC_VERSION}", "cachedVersion": "${MC_VERSION}", "important": true }
  ]
}
EOF
python3 - "${TMP_ROOT}/pack.zip" "${TMP_ROOT}/pack" <<'PY'
import os
import sys
import zipfile

with zipfile.ZipFile(sys.argv[1], "w") as archive:
    for root, _, files in os.walk(sys.argv[2]):
        for name in files:
            path = os.path.join(root, name)
            archive.write(path, os.path.relpath(path, sys.argv[2]))
PY
run_cmd instance create --name spike-imported --from "${TMP_ROOT}/pack.zip" --no-download
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["id"] == "spike-imported"'
check_json 'd["name"] == "spike-imported"'
expect_file "${DATA_DIR}/instances/spike-imported/instance.cfg"

# The paths of the client are relative to the directory the command was
# started in - the launcher itself runs in the data path.
step "instance create --from (a relative path of the client)"
run_cmd_in "${TMP_ROOT}" instance create --name spike-relative --from "pack.zip" --no-download
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["id"] == "spike-relative"'
expect_file "${DATA_DIR}/instances/spike-relative/instance.cfg"
run_cmd instance delete spike-relative --purge
expect_rc 0

step "instance list (the imported instance is there)"
run_cmd instance list
expect_rc 0
check_json 'any(i["id"] == "spike-imported" for i in d["instances"])'

step "instance delete (the imported instance)"
run_cmd instance delete spike-imported --purge
expect_rc 0
check_json 'd["ok"] is True'

step "instance duplicate (a copy for the trash test)"
run_cmd instance duplicate "${INSTANCE_ID}" --name spike-trash
expect_rc 0
check_json 'd["ok"] is True'
expect_file "${DATA_DIR}/instances/spike-trash/instance.cfg"

step "instance delete (the trash is used when --purge is not given)"
run_cmd instance delete spike-trash
if [ "${LAST_RC}" = "0" ]; then
    check_json 'd["ok"] is True'
    check_json 'd["method"] == "trash"'
    check_json 'd["purged"] is False'
    expect_no_file "${DATA_DIR}/instances/spike-trash/instance.cfg"
else
    # The trash is not available everywhere (a headless machine has no
    # desktop) - the command has to fail then, it must never delete the files
    # permanently on its own.
    expect_rc 5
    check_json 'd["ok"] is False'
    check_json 'd["error"]["code"] == "trash_failed"'
    expect_file "${DATA_DIR}/instances/spike-trash/instance.cfg"
fi

if [ "${FULL_DOWNLOAD:-0}" = "1" ]; then
    # The download of the files of an instance takes a while, so it is only
    # done when the test is asked for it.
    step "instance create (the files are downloaded)"
    run_cmd instance create --name spike-downloaded --mc "${MC_VERSION}" --loader fabric --loader-version "${FABRIC_LOADER}"
    expect_rc 0
    check_json 'd["ok"] is True'
    check_json 'd["downloaded"] is True'
    expect_file "${DATA_DIR}/libraries/com/mojang/minecraft/${MC_VERSION}/minecraft-${MC_VERSION}-client.jar"
    run_cmd instance delete spike-downloaded --purge
    expect_rc 0
fi

echo
echo "All smoke test steps passed."
