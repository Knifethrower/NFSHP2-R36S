#!/bin/sh
# Builds the R36S port in Docker: r36s/build/<arch>/nfshp2.<arch>.
#
#   r36s/build.sh [make arguments]           the armhf build, the target
#   ARCH=aarch64 r36s/build.sh [arguments]   the 64-bit one
#
# armhf's IMAGE comes from r36s/Dockerfile.armhf (nfshp2-r36s-build-armhf);
# aarch64's from r36s/Dockerfile (nfshp2-r36s-build), or the Burnout 3
# port's takedawn-r36s-build:debian10, which has the same.
set -eu
here=$(cd "$(dirname "$0")" && (pwd -W 2>/dev/null || pwd))
repo=$(dirname "$here")
ARCH=${ARCH:-armhf}
if [ "$ARCH" = armhf ]; then
    IMAGE=${IMAGE:-nfshp2-r36s-build-armhf}
else
    IMAGE=${IMAGE:-nfshp2-r36s-build}
    if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        IMAGE=takedawn-r36s-build:debian10
    fi
fi
MSYS_NO_PATHCONV=1 docker run --rm     -v "$repo/3DS Files:/game:ro"     -v "$here:/r36s"     -w /r36s "$IMAGE" make -j"${JOBS:-8}" ARCH="$ARCH" "$@"
