FROM fedora:latest

# Toolchain dla PrismLauncher (Qt6 >= 6.8) + JDK (kompilacja libraries/launcher)
RUN dnf install -y --setopt=install_weak_deps=False \
        git cmake ninja-build gcc-c++ extra-cmake-modules \
        qt6-qtbase-devel qt6-qtnetworkauth-devel \
        cmark-devel qrencode-devel libarchive-devel \
        tomlplusplus-devel zlib-ng-compat-devel cli11-devel \
        java-openjdk-devel \
    && dnf clean all

# git w kontenerze: repo zamontowane z hosta
RUN git config --global --add safe.directory '*'

WORKDIR /repo
CMD ["/bin/bash"]
