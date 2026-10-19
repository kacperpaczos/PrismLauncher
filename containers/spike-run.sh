#!/usr/bin/env bash
# Uruchamia komendę w kontenerze spike (repo podmontowane jako /repo).
# Użycie: ./containers/spike-run.sh "<komenda>"
set -euo pipefail

REPO_HOST="$(cd "$(dirname "$0")/.." && pwd)"

podman run --rm --security-opt label=disable \
    -v "$REPO_HOST:/repo" -w /repo -e HOME=/tmp localhost/prism-spike-dev \
    bash -c "QT_QPA_PLATFORM=offscreen $1"
