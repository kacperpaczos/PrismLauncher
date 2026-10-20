#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
#
# SPDX-License-Identifier: GPL-3.0-only
#
# End to end smoke test of the headless world, server, log, screenshot and
# notes commands of the CLI spike. The test fabricates a minimal instance in a
# throwaway data directory and runs
# `world|server|log|screenshot|notes list|read|info|copy|rename|delete|import|add|remove|set|show`
# against it. Every command works on the files of the instance, so the test
# does not need the network.
#
# Usage (inside the build container):
#   tests/cli/spike-domain-smoke.sh [path of the launcher binary]
#
# Environment:
#   PRISM_BIN  path of the launcher binary (default: <repo>/build/prismlauncher)
#   KEEP_TMP   set to 1 to keep the temporary data directory

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PRISM_BIN="${1:-${PRISM_BIN:-${REPO_ROOT}/build/prismlauncher}}"

INSTANCE_ID="spike-domain-test"
MC_VERSION="1.21.1"

TMP_ROOT="$(mktemp -d -t prism-domain-smoke-XXXXXX)"
export HOME="${TMP_ROOT}/home"
export XDG_DATA_HOME="${HOME}/.local/share"
DATA_DIR="${XDG_DATA_HOME}/PrismLauncher"
INSTANCE_DIR="${DATA_DIR}/instances/${INSTANCE_ID}"
GAME_DIR="${INSTANCE_DIR}/minecraft"
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

# expect_world <folder> - the world folder has to be there.
expect_world() {
    if [ ! -d "${GAME_DIR}/saves/$1" ]; then
        fail "the world '$1' is not in ${GAME_DIR}/saves"
    fi
}

expect_no_world() {
    if [ -d "${GAME_DIR}/saves/$1" ]; then
        fail "the world '$1' is still in ${GAME_DIR}/saves"
    fi
}

# --------------------------------------------------------------------------
# instance fabrication
# --------------------------------------------------------------------------

# write_level_dat <path> <name> <game type> - writes the level.dat of a
# world (the gzipped NBT of a compound with a `Data` compound in it).
write_level_dat() {
    python3 - "$1" "$2" "$3" <<'PY'
import gzip
import struct
import sys


def name(value):
    return struct.pack(">H", len(value)) + value.encode("utf-8")


def tag_string(key, value):
    data = value.encode("utf-8")
    return b"\x08" + name(key) + struct.pack(">H", len(data)) + data


def tag_int(key, value):
    return b"\x03" + name(key) + struct.pack(">i", value)


def tag_long(key, value):
    return b"\x04" + name(key) + struct.pack(">q", value)


path, level_name, game_type = sys.argv[1], sys.argv[2], int(sys.argv[3])
data = tag_string("LevelName", level_name)
data += tag_int("GameType", game_type)
data += tag_long("LastPlayed", 1700000000000)
data += tag_long("RandomSeed", 123456789)
data += b"\x00"
inner = b"\x0a" + name("Data") + data
root = b"\x0a" + name("") + inner + b"\x00"
with gzip.open(path, "wb") as handle:
    handle.write(root)
PY
}

create_instance() {
    mkdir -p "${GAME_DIR}/saves" "${GAME_DIR}/logs" "${GAME_DIR}/screenshots"

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

    # two worlds - one of them is addressed by its folder, the other one by
    # the name in its level.dat
    mkdir -p "${GAME_DIR}/saves/Test World"
    write_level_dat "${GAME_DIR}/saves/Test World/level.dat" "Test World" 0
    mkdir -p "${GAME_DIR}/saves/Second"
    write_level_dat "${GAME_DIR}/saves/Second/level.dat" "Another World" 1

    # a log of the instance
    cat > "${GAME_DIR}/logs/latest.log" <<'EOF'
[12:00:00] [main/INFO]: Starting minecraft
[12:00:01] [main/DEBUG]: Loading config
[12:00:02] [main/WARN]: Could not find resource pack
[12:00:03] [main/ERROR]: Failed to load world
EOF

    # a screenshot of the instance
    python3 - "${GAME_DIR}/screenshots/shot.png" <<'PY'
import struct
import sys
import zlib


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"\x00\xff\x00\x00"))
png += chunk(b"IEND", b"")
with open(sys.argv[1], "wb") as handle:
    handle.write(png)
PY
}

# --------------------------------------------------------------------------

create_instance

step "world list"
run_cmd world list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["instance"] == "spike-domain-test"'
check_json 'd["total"] == 2'
check_json 'any(w["folder"] == "Test World" and w["name"] == "Test World" for w in d["worlds"])'
check_json 'any(w["folder"] == "Second" and w["name"] == "Another World" for w in d["worlds"])'
check_json 'all(w["gameMode"] and w["gameModeRaw"] is not None for w in d["worlds"])'
check_json 'all("sizeBytes" not in w for w in d["worlds"])'

step "world list --size"
run_cmd world list -i "${INSTANCE_ID}" --size
expect_rc 0
check_json 'all(w["sizeBytes"] > 0 for w in d["worlds"])'

step "world info (by the folder)"
run_cmd world info "Test World" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["world"]["folder"] == "Test World"'
check_json 'd["world"]["name"] == "Test World"'
check_json 'd["world"]["gameMode"] == "Survival"'
check_json 'd["world"]["seed"] == 123456789'
check_json 'd["world"]["warnings"] == []'

step "world info (by the name of the level.dat)"
run_cmd world info "Another World" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["world"]["folder"] == "Second"'
check_json 'd["world"]["gameMode"] == "Creative"'

step "world info (a world that does not exist)"
run_cmd world info "does not exist" -i "${INSTANCE_ID}"
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "world_not_found"'

step "world copy"
run_cmd world copy "Test World" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["copied"]["name"] == "Test World"'
check_json 'd["copied"]["folder"] != "Test World"'
expect_world "Test World(1)"

step "world copy --name"
run_cmd world copy "Test World" -i "${INSTANCE_ID}" --name "The Copy"
expect_rc 0
check_json 'd["copied"]["name"] == "The Copy"'
check_json 'd["copied"]["folder"] == "The Copy"'
expect_world "The Copy"

step "world rename"
run_cmd world rename "The Copy" -i "${INSTANCE_ID}" --name "The Renamed Copy"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["renamed"]["name"] == "The Renamed Copy"'
check_json 'd["renamed"]["folder"] == "The Renamed Copy"'
expect_no_world "The Copy"
expect_world "The Renamed Copy"

step "world delete --permanent"
run_cmd world delete "The Renamed Copy" -i "${INSTANCE_ID}" --permanent
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["deleted"]["name"] == "The Renamed Copy"'
check_json 'd["deleted"]["permanent"] is True'
expect_no_world "The Renamed Copy"

step "world import (a zip with the world in a subfolder)"
mkdir -p "${TMP_ROOT}/staging/Nested World"
write_level_dat "${TMP_ROOT}/staging/Nested World/level.dat" "Nested World" 2
python3 - "${TMP_ROOT}/world.zip" "${TMP_ROOT}/staging" <<'PY'
import os
import sys
import zipfile

with zipfile.ZipFile(sys.argv[1], "w") as archive:
    for root, _, files in os.walk(sys.argv[2]):
        for name in files:
            path = os.path.join(root, name)
            archive.write(path, os.path.relpath(path, sys.argv[2]))
PY
run_cmd world import "${TMP_ROOT}/world.zip" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["imported"]["name"] == "Nested World"'
expect_world "Nested World"

step "world import (a file that is not a world)"
run_cmd world import "${GAME_DIR}/logs/latest.log" -i "${INSTANCE_ID}"
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "invalid_world"'

step "world import (a folder)"
mkdir -p "${TMP_ROOT}/staging/Folder World"
write_level_dat "${TMP_ROOT}/staging/Folder World/level.dat" "Folder World" 0
run_cmd world import "${TMP_ROOT}/staging/Folder World" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["imported"]["name"] == "Folder World"'
expect_world "Folder World"

step "world delete (to the trash)"
run_cmd world delete "Folder World" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["deleted"]["permanent"] is False'
expect_no_world "Folder World"

step "server add"
run_cmd server add "Hypixel" "mc.hypixel.net" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["added"]["name"] == "Hypixel"'
check_json 'd["added"]["address"] == "mc.hypixel.net"'
check_json 'd["added"]["acceptsTextures"] == "ask"'
check_json 'd["added"]["hasIcon"] is False'

step "server add (with an index and a resource pack mode)"
run_cmd server add "Second" "example.com:25565" -i "${INSTANCE_ID}" --index 0 --accepts-textures never
expect_rc 0
check_json 'd["added"]["index"] == 0'
check_json 'd["added"]["acceptsTextures"] == "never"'

step "server list"
run_cmd server list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] == 2'
check_json 'd["servers"][0]["name"] == "Second"'
check_json 'd["servers"][1]["name"] == "Hypixel"'
check_json 'all("players" not in s for s in d["servers"])'

step "server remove (by the index)"
run_cmd server remove 0 -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'len(d["removed"]) == 1'
check_json 'd["removed"][0]["name"] == "Second"'

step "server remove (by the name)"
run_cmd server remove "Hypixel" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["removed"][0]["name"] == "Hypixel"'

step "server list (the list is empty again)"
run_cmd server list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["total"] == 0'

step "server remove (a server that does not exist)"
run_cmd server remove "does not exist" -i "${INSTANCE_ID}"
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "server_not_found"'

step "notes show (the notes are empty)"
run_cmd notes show -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["notes"] == ""'

step "notes set"
run_cmd notes set "the first note" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["notes"] == "the first note"'

step "notes show"
run_cmd notes show -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["notes"] == "the first note"'

step "notes set --append"
run_cmd notes set "the second note" -i "${INSTANCE_ID}" --append
expect_rc 0
check_json 'd["notes"] == "the first note\nthe second note"'

step "notes set --file"
printf 'from a file' > "${TMP_ROOT}/notes.txt"
run_cmd notes set -i "${INSTANCE_ID}" --file "${TMP_ROOT}/notes.txt"
expect_rc 0
check_json 'd["notes"] == "from a file"'

step "notes set --file (the text replaces the old notes)"
printf 'another file' > "${TMP_ROOT}/notes2.txt"
run_cmd notes set -i "${INSTANCE_ID}" --file "${TMP_ROOT}/notes2.txt"
expect_rc 0
check_json 'd["notes"] == "another file"'

step "notes set --file --append"
printf 'and more' > "${TMP_ROOT}/notes3.txt"
run_cmd notes set -i "${INSTANCE_ID}" --file "${TMP_ROOT}/notes3.txt" --append
expect_rc 0
check_json 'd["notes"] == "another file\nand more"'

step "notes set --file (the line break at the end of the file is dropped)"
printf 'with a break\n' > "${TMP_ROOT}/notes4.txt"
run_cmd notes set -i "${INSTANCE_ID}" --file "${TMP_ROOT}/notes4.txt"
expect_rc 0
check_json 'd["notes"] == "with a break"'

step "notes set (a file that does not exist)"
run_cmd notes set -i "${INSTANCE_ID}" --file "${TMP_ROOT}/nope.txt"
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "notes_not_read"'

step "log list"
run_cmd log list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] >= 1'
check_json 'any(l["file"] == "logs/latest.log" for l in d["logs"])'

step "log read (the raw lines)"
run_cmd log read -i "${INSTANCE_ID}"
expect_rc 0
if ! printf '%s' "${LAST_OUT}" | grep -q "Starting minecraft"; then
    echo "   output: ${LAST_OUT}" >&2
    fail "the raw lines of the log were not written to the output"
fi

step "log read --json"
run_cmd log read -i "${INSTANCE_ID}" --json
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["file"] == "logs/latest.log"'
check_json 'len(d["lines"]) == 4'
check_json 'd["lines"][0]["n"] == 1'
check_json 'd["lines"][0]["level"] == "info"'
check_json 'd["truncated"] is False'

step "log read --tail"
run_cmd log read -i "${INSTANCE_ID}" --tail 2 --json
expect_rc 0
check_json 'len(d["lines"]) == 2'
check_json 'd["lines"][0]["n"] == 3'
check_json 'd["truncated"] is True'

step "log read --level"
run_cmd log read -i "${INSTANCE_ID}" --level warning --json
expect_rc 0
check_json 'len(d["lines"]) == 2'
check_json 'all(l["level"] in ("warning", "error") for l in d["lines"])'

step "log read --grep"
run_cmd log read -i "${INSTANCE_ID}" --grep "ERROR" --json
expect_rc 0
check_json 'len(d["lines"]) == 1'
check_json '"ERROR" in d["lines"][0]["text"]'

step "log read (a log that does not exist)"
run_cmd log read -i "${INSTANCE_ID}" --file "logs/nope.log"
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "log_not_found"'

step "screenshot list"
run_cmd screenshot list -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["total"] == 1'
check_json 'd["screenshots"][0]["id"] == "shot.png"'

step "screenshot copy"
run_cmd screenshot copy "shot.png" "${TMP_ROOT}/copy.png" -i "${INSTANCE_ID}"
expect_rc 0
check_json 'd["ok"] is True'
check_json 'd["id"] == "shot.png"'
expect_file "${TMP_ROOT}/copy.png"

step "screenshot copy (the file is not overwritten)"
run_cmd screenshot copy "shot.png" "${TMP_ROOT}/copy.png" -i "${INSTANCE_ID}"
expect_rc 4
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "file_exists"'

step "screenshot copy --force"
run_cmd screenshot copy "shot.png" "${TMP_ROOT}/copy.png" -i "${INSTANCE_ID}" --force
expect_rc 0
check_json 'd["ok"] is True'

step "screenshot copy (a screenshot that does not exist)"
run_cmd screenshot copy "nope.png" "${TMP_ROOT}/nope.png" -i "${INSTANCE_ID}"
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "screenshot_not_found"'

step "a command of an instance that does not exist"
run_cmd world list -i does-not-exist
expect_rc 3
check_json 'd["ok"] is False'
check_json 'd["error"]["code"] == "instance_not_found"'

echo
echo "All smoke test steps passed."
