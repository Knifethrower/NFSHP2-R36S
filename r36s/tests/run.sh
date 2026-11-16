#!/bin/sh
# Builds and runs the R36S port's unit tests for aarch64 in the build image
# (under Docker's QEMU on a PC). Usage: sh r36s/tests/run.sh
set -e
here=$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))
IMAGE=${IMAGE:-takedawn-r36s-build:debian10}
MSYS_NO_PATHCONV=1 docker run --rm -v "$here:/r36s" -w /r36s $IMAGE sh -c '
  mkdir -p build/tests &&
  aarch64-linux-gnu-gcc -std=gnu11 -O2 -g -no-pie -fno-pie -Wall -Wno-unused-function \
    tests/lowheap_test.c -o build/tests/lowheap_test -lpthread &&
  ./build/tests/lowheap_test'
