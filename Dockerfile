FROM ubuntu:24.04 AS base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        adwaita-icon-theme \
        autoconf \
        automake \
        autopoint \
        bash \
        bison \
        build-essential \
        ca-certificates \
        cmake \
        cpio \
        curl \
        e2fsprogs \
        file \
        flex \
        gawk \
        gperf \
        git \
        gettext \
        glib-networking \
        gstreamer1.0-libav \
        gstreamer1.0-gl \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-base \
        gstreamer1.0-plugins-good \
        gstreamer1.0-tools \
        libevdev2 \
        libexpat1-dev \
        libgdk-pixbuf2.0-bin \
        libgmp-dev \
        libsecret-1-0 \
        libltdl-dev \
        libtool \
        m4 \
        libmpc-dev \
        libmpfr-dev \
        make \
        meson \
        ninja-build \
        pkg-config \
        python3 \
        python3-mako \
        python3-packaging \
        python3-pip \
        python3-yaml \
        qemu-system-x86 \
        rsync \
        sparse \
        tar \
        texinfo \
        unzip \
        wget \
        xkb-data \
        xz-utils \
        zlib1g-dev \
    && python3 -m pip install --break-system-packages --no-cache-dir 'meson>=1.4,<2' \
    && rm -rf /var/lib/apt/lists/*

COPY scripts/container-xv6-command.sh /usr/local/bin/xv6-command
COPY scripts/container-hints.sh /usr/local/bin/xv6-hints
RUN chmod 0755 /usr/local/bin/xv6-command \
    && chmod 0755 /usr/local/bin/xv6-hints \
    && ln -s xv6-command /usr/local/bin/xv6-kernel-x86 \
    && ln -s xv6-command /usr/local/bin/xv6-build \
    && ln -s xv6-command /usr/local/bin/xv6-help \
    && ln -s xv6-command /usr/local/bin/xv6-user-ports \
    && ln -s xv6-command /usr/local/bin/xv6-images \
    && ln -s xv6-command /usr/local/bin/xv6-launch-nokvm \
    && ln -s xv6-command /usr/local/bin/xv6-qemu-nokvm \
    && printf '\n# xv6 command hints\nif [[ $- == *i* && -r /usr/local/bin/xv6-hints ]]; then\n    . /usr/local/bin/xv6-hints\nfi\n' >> /etc/bash.bashrc

WORKDIR /src/xv6-os

FROM base AS build

ARG XV6_ARCH=x86_64
ARG XV6_PARALLEL_JOBS=2
ARG BUILD_TARGET=world
ARG BUILD_DIR=/build/xv6-os
ARG XV6_WEBKIT_REF_SYSROOT=

COPY . /src/xv6-os

RUN test -f kernel/CMakeLists.txt \
    && test -f user/CMakeLists.txt \
    && test -f ports/CMakeLists.txt

RUN set -eux; \
    webkit_args=""; \
    if [ -n "${XV6_WEBKIT_REF_SYSROOT}" ]; then \
        webkit_args="-DXV6_WEBKIT_REF_SYSROOT=${XV6_WEBKIT_REF_SYSROOT} -DXV6_WEBKIT_STRICT_STAGE=ON"; \
    fi; \
    cmake -S /src/xv6-os -B "${BUILD_DIR}" \
        -G Ninja \
        -DXV6_ARCH="${XV6_ARCH}" \
        -DXV6_PARALLEL_JOBS="${XV6_PARALLEL_JOBS}" \
        ${webkit_args}

RUN cmake --build "${BUILD_DIR}" --target "${BUILD_TARGET}"

FROM base AS dev

ARG XV6_ARCH=x86_64
ARG XV6_PARALLEL_JOBS=2
ARG BUILD_DIR=/build/xv6-os
ARG XV6_WEBKIT_REF_SYSROOT=

COPY . /src/xv6-os

RUN set -eux; \
    webkit_args=""; \
    if [ -n "${XV6_WEBKIT_REF_SYSROOT}" ]; then \
        webkit_args="-DXV6_WEBKIT_REF_SYSROOT=${XV6_WEBKIT_REF_SYSROOT} -DXV6_WEBKIT_STRICT_STAGE=ON"; \
    fi; \
    cmake -S /src/xv6-os -B "${BUILD_DIR}" \
        -G Ninja \
        -DXV6_ARCH="${XV6_ARCH}" \
        -DXV6_PARALLEL_JOBS="${XV6_PARALLEL_JOBS}" \
        ${webkit_args}

WORKDIR /src/xv6-os

CMD ["xv6-help"]
