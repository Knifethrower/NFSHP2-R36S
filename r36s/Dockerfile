# The R36S build image: Debian 10 (glibc 2.28, PortMaster's usual
# baseline), the aarch64 cross compiler, the arm64 GLES/EGL headers and
# SDL 2.28 with the KMSDRM and ALSA back ends, loaded at run time.
#
#   docker build -t nfshp2-r36s-build r36s
FROM --platform=linux/amd64 debian:10-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's|deb.debian.org|archive.debian.org|g; s|security.debian.org|archive.debian.org|g' /etc/apt/sources.list \
    && dpkg --add-architecture arm64 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates curl file make ninja-build pkg-config python3 python3-pip \
        gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
        libasound2-dev:arm64 libdrm-dev:arm64 libegl1-mesa-dev:arm64 \
        libgbm-dev:arm64 libgles2-mesa-dev:arm64 libudev-dev:arm64 \
    && python3 -m pip install --no-cache-dir 'cmake==3.27.9' \
    && rm -rf /var/lib/apt/lists/*

RUN curl --fail --location --retry 3 \
        https://github.com/libsdl-org/SDL/releases/download/release-2.28.5/SDL2-2.28.5.tar.gz \
        --output /tmp/SDL2.tar.gz \
    && tar -xzf /tmp/SDL2.tar.gz -C /tmp \
    && PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig \
       cmake -S /tmp/SDL2-2.28.5 -B /tmp/sdl-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
        -DCMAKE_INSTALL_PREFIX=/opt/r36s \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
        -DSDL_KMSDRM=ON -DSDL_KMSDRM_SHARED=ON -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON \
        -DSDL_PULSEAUDIO=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF \
        -DSDL_OPENGL=OFF -DSDL_OPENGLES=ON \
    && cmake --build /tmp/sdl-build --parallel 4 \
    && cmake --install /tmp/sdl-build \
    && cp /tmp/SDL2-2.28.5/LICENSE.txt /opt/r36s/LICENSE-SDL2.txt \
    && rm -rf /tmp/SDL2.tar.gz /tmp/SDL2-2.28.5 /tmp/sdl-build

WORKDIR /r36s
