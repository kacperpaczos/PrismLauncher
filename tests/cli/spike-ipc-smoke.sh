#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
#
# SPDX-License-Identifier: GPL-3.0-only
#
# End to end smoke test of the IPC of the CLI spike. The test starts a
# launcher (the GUI one, offscreen) in a throwaway data directory and runs
# the headless commands of the CLI against it. The commands are executed by
# the running launcher and their answers are sent back over the IPC, so the
# output of the client is the same as the output of a headless run.
#
# The test does not need the network.
#
# Usage (inside the build container):
#   tests/cli/spike-ipc-smoke.sh [path of the launcher binary]
#
# Environment:
#   PRISM_BIN  path of the launcher binary (default: <repo>/build/prismlauncher)
#   KEEP_TMP   set to 1 to keep the temporary data directory

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRISM_BIN="${1:-${PRISM_BIN:-${REPO_ROOT}/build/prismlauncher}}"

INSTANCE_ID="spike-ipc-test"
MC_VERSION="1.21.1"

TMP_ROOT="$(mktemp -d -t prism-ipc-smoke-XXXXXX)"
export HOME="${TMP_ROOT}/home"
export XDG_DATA_HOME="${HOME}/.local/share"
DATA_DIR="${TMP_ROOT}/data"
INSTANCE_DIR="${DATA_DIR}/instances/${INSTANCE_ID}"
GAME_DIR="${INSTANCE_DIR}/minecraft"
LOG_FILE="${TMP_ROOT}/launcher.log"
GUI_LOG="${TMP_ROOT}/gui.log"
JSON_FILE="${TMP_ROOT}/output.json"

GUI_PID=""

cleanup() {
    if [ -n "${GUI_PID}" ]; then
        kill "${GUI_PID}" 2>/dev/null
        wait "${GUI_PID}" 2>/dev/null
    fi
    if [ "${KEEP_TMP:-0}" = "1" ]; then
        echo "Temporary directory kept at ${TMP_ROOT}"
    else
        rm -rf "${TMP_ROOT}"
    fi
}
trap cleanup EXIT

fail() {
    echo "FAIL: $*" >&2
    if [ -f "${GUI_LOG}" ]; then
        echo "--- the log of the launcher (tail) ---" >&2
        tail -n 40 "${GUI_LOG}" >&2 || true
    fi
    if [ -f "${LOG_FILE}" ]; then
        echo "--- the log of the clients (tail) ---" >&2
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
# helpers
# --------------------------------------------------------------------------
LAST_OUT=""
LAST_RC=0

run_cmd() {
    local out rc
    out="$(QT_QPA_PLATFORM=offscreen "${PRISM_BIN}" -d "${DATA_DIR}" "$@" 2>>"${LOG_FILE}")"
    rc=$?
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

# check_json <python expression> [arguments] - the expression is evaluated with
# the parsed JSON document bound to `d`. The `%s` of the expression are replaced
# with the arguments.
check_json() {
    local expr="$1"
    shift
    printf '%s' "${LAST_OUT}" > "${JSON_FILE}"
    if ! python3 - "${JSON_FILE}" "${expr}" "$@" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    raw = handle.read()
expr = sys.argv[2]
for argument in sys.argv[3:]:
    expr = expr.replace("%s", argument)
try:
    d = json.loads(raw)
except Exception as exc:
    print("FAIL: the output is not valid JSON: %r (%s)" % (raw, exc))
    sys.exit(1)
try:
    ok = eval(expr, {"d": d, "len": len, "any": any, "all": all})
except Exception as exc:
    print("FAIL: could not evaluate %r: %s" % (expr, exc))
    print("output: %s" % raw)
    sys.exit(1)
if not ok:
    print("FAIL: the assertion %r failed" % expr)
    print("output: %s" % raw)
    sys.exit(1)
PY
    then
        echo "--- the log of the launcher (tail) ---" >&2
        tail -n 60 "${GUI_LOG}" >&2 || true
        echo "--- the log of the clients (tail) ---" >&2
        tail -n 60 "${LOG_FILE}" >&2 || true
        exit 1
    fi
}

# --------------------------------------------------------------------------
# the data directory of the launcher
# --------------------------------------------------------------------------
mkdir -p "${DATA_DIR}" "${HOME}"

# The settings that keep the launcher from showing the setup wizard (the
# language, the java and the themes) and the account that keeps it from
# showing the login page.
cat > "${DATA_DIR}/prismlauncher.cfg" <<EOF
Language=en
IgnoreJavaWizard=true
UserAskedAboutAutomaticJavaDownload=true
ApplicationTheme=system
IconTheme=pe_colored
EOF

cat > "${DATA_DIR}/accounts.json" <<'EOF'
{
  "formatVersion": 3,
  "accounts": [
    {
      "type": "MSA",
      "profile": {
        "id": "00000000000000000000000000000001",
        "name": "SpikeTester",
        "skin": {
          "id": "00000000000000000000000000000002",
          "url": "https://example.invalid/skin.png",
          "variant": "classic"
        },
        "capes": []
      },
      "active": true
    }
  ]
}
EOF

# an instance with a world and a note
mkdir -p "${GAME_DIR}/saves/Test World" "${GAME_DIR}/logs"
cat > "${INSTANCE_DIR}/instance.cfg" <<EOF
[Instance]
InstanceType=OneSix
name=${INSTANCE_ID}
iconKey=default
uuid=44444444-5555-6666-7777-888888888888
EOF
cat > "${INSTANCE_DIR}/mmc-pack.json" <<EOF
{
  "formatVersion": 1,
  "components": [
    { "uid": "net.minecraft", "version": "${MC_VERSION}", "cachedVersion": "${MC_VERSION}" }
  ]
}
EOF
cat > "${INSTANCE_DIR}/instance.cfg" <<EOF
[Instance]
InstanceType=OneSix
name=${INSTANCE_ID}
iconKey=default
uuid=44444444-5555-6666-7777-888888888888
notes=a note of the instance
EOF

# --------------------------------------------------------------------------
# the launcher
# --------------------------------------------------------------------------
step "starting the launcher"
QT_QPA_PLATFORM=offscreen "${PRISM_BIN}" -d "${DATA_DIR}" >"${GUI_LOG}" 2>&1 &
GUI_PID=$!

step "waiting for the launcher to take the lock"
# The launcher takes the lock of the data directory before it writes its log.
# A client that starts before that would become the server itself - so the
# test has to wait for the log of the launcher.
locked=0
for _ in $(seq 1 60); do
    if ! kill -0 "${GUI_PID}" 2>/dev/null; then
        fail "the launcher died - see ${GUI_LOG}"
    fi
    if [ -s "${DATA_DIR}/logs/PrismLauncher-0.log" ]; then
        locked=1
        break
    fi
    sleep 1
done
if [ "${locked}" != "1" ]; then
    fail "the launcher did not start"
fi

step "waiting for the launcher to be ready"
ready=0
for _ in $(seq 1 60); do
    if ! kill -0 "${GUI_PID}" 2>/dev/null; then
        fail "the launcher died - see ${GUI_LOG}"
    fi
    run_cmd --ipc-timeout 5 status
    if [ "${LAST_RC}" = "0" ]; then
        ready=1
        break
    fi
    sleep 1
done
if [ "${ready}" != "1" ]; then
    fail "the launcher did not become ready"
fi

# --------------------------------------------------------------------------
# the commands that the running launcher answers
# --------------------------------------------------------------------------
step "status"
run_cmd status
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["version"]'
check_json 'd["dataPath"] == "%s"' "${DATA_DIR}"
check_json 'len(d["instances"]) == 1'
check_json 'd["instances"][0]["id"] == "spike-ipc-test"'
check_json 'd["instances"][0]["name"] == "spike-ipc-test"'
# the launcher answers - it knows that the instance is not running
check_json 'd["instances"][0]["running"] is False'
check_json 'len(d["accounts"]) == 1'
check_json 'd["accounts"][0]["name"] == "SpikeTester"'
check_json 'd["accounts"][0]["type"] == "msa"'

step "instance list"
run_cmd instance list
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] == 1'
check_json 'd["instances"][0]["id"] == "spike-ipc-test"'

step "notes show"
run_cmd notes show -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["notes"] == "a note of the instance"'

step "notes set (the launcher writes the file)"
run_cmd notes set "another note" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
if ! grep -q "another note" "${INSTANCE_DIR}/instance.cfg"; then
    fail "the launcher did not write the notes of the instance"
fi

step "a command of an instance that does not exist"
run_cmd world list -i does-not-exist
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "instance_not_found"'

step "the launcher is still running (K1)"
if ! kill -0 "${GUI_PID}" 2>/dev/null; then
    fail "the launcher died while it served the requests"
fi

step "the launcher is still running after a failing command"
run_cmd world list -i does-not-exist
if ! kill -0 "${GUI_PID}" 2>/dev/null; then
    fail "the launcher died while it served the requests"
fi

# --------------------------------------------------------------------------
# the launcher is gone - the commands work on their own
# --------------------------------------------------------------------------
step "stopping the launcher"
kill "${GUI_PID}" 2>/dev/null
wait "${GUI_PID}" 2>/dev/null
GUI_PID=""

step "status (the launcher does not run - the state is gathered locally)"
run_cmd status
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["instances"]) == 1'
# the CLI is a process of its own - it cannot see the running instances
check_json 'd["instances"][0]["running"] is None'

step "instance list (the launcher does not run)"
run_cmd instance list
expect_rc 0
check_json 'd["total"] == 1'

echo
echo "All smoke test steps passed."
