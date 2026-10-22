#!/usr/bin/env bash
# Verify (inside the build container): when the download batch of
# `mod update` fails on ONE mod, the disabled mod that WAS downloaded
# successfully still gets its `.disabled` state restored.
#
# Scenario: two mods with available updates - jei (DISABLED) and
# fabric-api (enabled). The download target of fabric-api is blocked with a
# directory, so exactly one download fails. Before the fix the whole
# restoration was skipped and jei came back enabled.
set -uo pipefail

REPO_ROOT="/repo"
PRISM_BIN="${PRISM_BIN:-${REPO_ROOT}/build/prismlauncher}"

INSTANCE_ID="spike-restore"
MC_VERSION="1.21.1"
FABRIC_LOADER="0.16.9"

TMP_ROOT="$(mktemp -d -t prism-restore-XXXXXX)"
export HOME="${TMP_ROOT}/home"
export XDG_DATA_HOME="${HOME}/.local/share"
DATA_DIR="${XDG_DATA_HOME}/PrismLauncher"
INSTANCE_DIR="${DATA_DIR}/instances/${INSTANCE_ID}"
MODS_DIR="${INSTANCE_DIR}/minecraft/mods"

cleanup() { rm -rf "${TMP_ROOT}"; }
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

LAST_OUT=""
LAST_RC=0
run_cmd() {
    set +e
    LAST_OUT="$(QT_QPA_PLATFORM=offscreen "${PRISM_BIN}" "$@" 2>"${TMP_ROOT}/stderr.log")"
    LAST_RC=$?
    set -e
    echo "   \$ prismlauncher $* -> exit ${LAST_RC}"
}

check_json() {
    printf '%s' "${LAST_OUT}" | python3 -c '
import json, sys
raw = sys.stdin.read()
expr = sys.argv[1]
d = json.loads(raw)
ok = eval(expr, {"d": d, "len": len, "any": any, "all": all})
if not ok:
    print("FAIL: assertion %r failed" % expr)
    print("output: %s" % raw[:2000])
    sys.exit(1)
' "$1" || exit 1
}

json_value() {
    printf '%s' "${LAST_OUT}" | python3 -c '
import json, sys
d = json.load(sys.stdin)
print(eval(sys.argv[1], {"d": d}))
' "$1"
}

# instance fabrication (the same as the smoke test)
mkdir -p "${MODS_DIR}"
cat > "${INSTANCE_DIR}/instance.cfg" <<EOF
[Instance]
InstanceType=OneSix
name=${INSTANCE_ID}
iconKey=default
uuid=11111111-2222-3333-4444-555555555555
EOF
cat > "${INSTANCE_DIR}/mmc-pack.json" <<EOF
{
  "formatVersion": 1,
  "components": [
    { "uid": "net.minecraft", "version": "${MC_VERSION}", "cachedVersion": "${MC_VERSION}" },
    { "uid": "net.fabricmc.fabric-loader", "version": "${FABRIC_LOADER}" }
  ]
}
EOF

echo "== resolve versions of the two mods"
run_cmd mod info jei
check_json 'd["ok"] is True'
JEI_OLD="$(json_value '[v["version_id"] for v in d["versions"] if "1.21.1-fabric" in v["file_name"]][1]')"
run_cmd mod info fabric-api
check_json 'd["ok"] is True'
FAB_OLD="$(json_value '[v["version_id"] for v in d["versions"] if "1.21.1" in v["file_name"]][1]')"
[ -n "${JEI_OLD}" ] || fail "no old jei version"
[ -n "${FAB_OLD}" ] || fail "no old fabric-api version"

echo "== install the old versions (one mod will be disabled)"
run_cmd mod install jei --version "${JEI_OLD}" --no-deps -i "${INSTANCE_ID}"
[ "${LAST_RC}" = 0 ] || fail "jei install failed"
run_cmd mod install fabric-api --version "${FAB_OLD}" --no-deps -i "${INSTANCE_ID}"
[ "${LAST_RC}" = 0 ] || fail "fabric-api install failed"

echo "== disable jei"
run_cmd mod disable jei -i "${INSTANCE_ID}"
[ "${LAST_RC}" = 0 ] || fail "jei disable failed"
ls "${MODS_DIR}" | grep -q '^jei.*\.jar\.disabled$' || fail "jei is not disabled"

echo "== dry run: learn the file names of the pending updates"
run_cmd mod update --all --no-deps --dry-run -i "${INSTANCE_ID}"
[ "${LAST_RC}" = 0 ] || fail "dry run failed"
FAB_NEW="$(json_value '[u["file"] for u in d["updated"] if u["slug"] == "fabric-api"][0]')"
[ -n "${FAB_NEW}" ] || fail "fabric-api has no update available"
echo "   blocked download target: ${FAB_NEW}"

echo "== block exactly one download (fabric-api) with a directory"
rm -f "${MODS_DIR}/${FAB_NEW}"
mkdir -p "${MODS_DIR}/${FAB_NEW}"

echo "== mod update (the batch must fail on fabric-api ...)"
run_cmd mod update --all --no-deps -i "${INSTANCE_ID}"
[ "${LAST_RC}" = 7 ] || fail "expected exit 7 (download_failed), got ${LAST_RC}"
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "download_failed"'
check_json 'any(u["slug"] == "jei" and u["enabled"] is False for u in d["updated"])'

echo "== ... but the disabled mod that was downloaded stays disabled"
ls "${MODS_DIR}" | grep -q '^jei.*\.jar\.disabled$' || fail "jei lost its .disabled state"
ls "${MODS_DIR}" | grep -E -q '^jei[^ ]*\.jar$' && fail "jei is enabled again (plain jar present)"
echo "   OK: $(ls "${MODS_DIR}" | grep '^jei')"

echo "== the failed download did not overwrite anything"
[ -d "${MODS_DIR}/${FAB_NEW}" ] || fail "the blocked target is gone (unexpected)"
echo "All verification steps passed."
