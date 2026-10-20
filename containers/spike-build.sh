#!/usr/bin/env bash
# Spike build w kontenerze (rootless podman; pliki wynikowe należą do użytkownika hosta).
# Użycie: ./containers/spike-build.sh [czyszczenie: clean|configure|build|test|all]
set -euo pipefail

REPO_HOST="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=prism-spike-dev
STAGE="${1:-all}"

# Worktrees keep their git metadata in the parent repository, and the .git
# file of the worktree points at an absolute path on the host. Both the
# worktree itself and the git directory have to be mounted under the same
# absolute path, otherwise git cannot resolve the repository inside the
# container at all ("gitfile does not point to a valid repository").
EXTRA_MOUNTS=()
if [[ -f "$REPO_HOST/.git" ]]; then
    GIT_COMMON="$(git -C "$REPO_HOST" rev-parse --git-common-dir 2>/dev/null || true)"
    if [[ -n "$GIT_COMMON" && -d "$GIT_COMMON" && "$GIT_COMMON" != "$REPO_HOST/.git" ]]; then
        GIT_COMMON="$(cd "$GIT_COMMON" && pwd)"
        EXTRA_MOUNTS+=(-v "$REPO_HOST:$REPO_HOST" -v "$GIT_COMMON:$GIT_COMMON")
    fi
fi

build_image() {
    podman build -t "$IMAGE" -f "$REPO_HOST/containers/Dockerfile" "$REPO_HOST/containers"
}

run_in() {
    # label=disable: SELinux on the host blocks the container from reading the
    # bind-mounted repo (user_home_t label) -> "Permission denied" on /repo.
    podman run --rm --security-opt label=disable \
        -v "$REPO_HOST:/repo" "${EXTRA_MOUNTS[@]}" -w /repo -e HOME=/tmp "$IMAGE" bash -c "$1"
}

case "$STAGE" in
    image)  build_image ;;
    submodules)
        # The submodule is only cloned when it is not there yet. Running the
        # update again rewrites the worktree of the submodule, and the path of
        # that worktree differs between the host and the container - which
        # breaks git on the host.
        run_in "test -e libraries/libnbtplusplus/.git || git submodule update --init --depth 1 libraries/libnbtplusplus"
        ;;
    configure)
        run_in "cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DBUILD_TESTING=ON \
            -DLauncher_BUILD_PLATFORM=dev \
            -DLauncher_ENABLE_JAVA_DOWNLOADER=ON"
        ;;
    build)
        run_in "cmake --build build -j\$(nproc)"
        ;;
    test)
        run_in "QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure"
        ;;
    all)
        build_image
        run_in "git submodule update --init --depth 1 libraries/libnbtplusplus && \
                cmake -B build -G Ninja \
                  -DCMAKE_BUILD_TYPE=Debug \
                  -DBUILD_TESTING=ON \
                  -DLauncher_BUILD_PLATFORM=dev \
                  -DLauncher_ENABLE_JAVA_DOWNLOADER=ON && \
                cmake --build build -j\$(nproc) && \
                QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure"
        ;;
    clean)
        run_in "rm -rf build"
        ;;
    *) echo "usage: $0 [image|submodules|configure|build|test|all|clean]"; exit 2 ;;
esac
