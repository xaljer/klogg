FROM ubuntu:jammy

ENV DEBIAN_FRONTEND=noninteractive

# Jammy's qt6-base-dev only recommends the OpenGL development packages, and the
# install below skips recommends, so Qt6Gui's find_dependency(WrapOpenGL) would
# fail without them (Noble pulls them in as hard dependencies).
# qt6-l10n-tools is the same story: Jammy does not pull it in, but Qt6LinguistTools
# needs /usr/lib/qt6/bin/lconvert at configure time.
RUN apt update -y && \
    apt install --no-install-recommends build-essential python3 python-is-python3 \
                    qt6-base-dev libqt6core5compat6-dev qt6-translations-l10n \
                    qt6-tools-dev-tools qt6-base-dev-tools qt6-tools-dev \
                    qt6-l10n-tools \
                    libglvnd-dev \
                    libboost-dev libssl-dev libicu-dev libcurl4-openssl-dev \
                    ragel ninja-build zlib1g-dev git pkg-config \
                    wget ca-certificates fuse file -y && \
    apt clean && rm -rf /var/lib/apt/lists/*

RUN wget https://github.com/Kitware/CMake/releases/download/v3.20.2/cmake-3.20.2-linux-x86_64.sh && \
    chmod 755 cmake-3.20.2-linux-x86_64.sh && \
    ./cmake-3.20.2-linux-x86_64.sh --prefix=/opt/ --exclude-subdir --skip-license && \
    rm cmake-3.20.2-linux-x86_64.sh

ENV PATH=/opt/bin:$PATH
