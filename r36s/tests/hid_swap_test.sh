#!/bin/sh
# Builds and runs hid_swap_test.c (the D-pad and left stick swap) for armhf
# in the build image, under Docker's QEMU on a PC, against the image's SDL:
# once as the game starts by default, once with NFSHP2_DPAD_STICK=1.
# Usage: sh r36s/tests/hid_swap_test.sh
set -e
here=$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))
repo=$(dirname "$here")
IMAGE=${IMAGE:-nfshp2-r36s-build-armhf}
MSYS_NO_PATHCONV=1 docker run --rm -v "$here:/r36s" -v "$repo/3DS Files:/game:ro" -w /r36s $IMAGE sh -c '
  mkdir -p build/tests &&
  arm-linux-gnueabihf-gcc -std=gnu11 -O2 -g -Wall -Wno-unused-function \
    -march=armv7ve -mfpu=neon-vfpv4 -mfloat-abi=hard -marm -D__3DS__ -DNFSHP2_R36S \
    -Icompat/include -Iplatform -I/game/source -I/opt/r36s/include/SDL2 -D_REENTRANT \
    tests/hid_swap_test.c compat/src/ctru_hid.c /game/source/joyhid.c /game/source/helpglyph.c \
    -o build/tests/hid_swap_test -L/opt/r36s/lib -lSDL2 &&
  export LD_LIBRARY_PATH=/opt/r36s/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy &&
  ./build/tests/hid_swap_test &&
  NFSHP2_DPAD_STICK=1 ./build/tests/hid_swap_test &&
  HID_TEST_REST=1 ./build/tests/hid_swap_test &&
  HID_TEST_REST=1 NFSHP2_STICK_CAL=0 ./build/tests/hid_swap_test &&
  HID_TEST_MAPS=1 ./build/tests/hid_swap_test &&
  NFSHP2_SWAP_AB=1 NFSHP2_SWAP_XY=1 ./build/tests/hid_swap_test &&
  rm -rf /tmp/hidroot && mkdir -p /tmp/hidroot/NFSHP2 &&
  NFSHP2_ROOT=/tmp/hidroot HID_TEST_FACE_MENU=1 ./build/tests/hid_swap_test &&
  cat /tmp/hidroot/NFSHP2/SAVES/face.txt'
