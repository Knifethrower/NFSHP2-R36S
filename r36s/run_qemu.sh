#!/bin/sh
# Runs the R36S build headless under Docker's QEMU on a card folder (the
# NFSHP2 folder's parent, from tools/make_card.py) and names the addresses
# of a crash report. The game sends stderr, where the report goes, to the
# card's nfshp2_stderr.txt; stdout lands in r36s/build/<arch>/run.log.
#
#   sh r36s/run_qemu.sh <card> [seconds]          the armhf build
#   ARCH=aarch64 sh r36s/run_qemu.sh <card> [s]   the 64-bit one
#
# Every NFSHP2_* variable set here reaches the game:
#   NFSHP2_INPUT    scripted pad presses (compat/src/ctru_hid.c), e.g.
#                   '300:A*20/120'
#   NFSHP2_GL       pbuffer: draw, with Mesa's software GLES in the armhf
#                   run image (r36s/Dockerfile.armhf-run). Without it
#                   nothing is drawn, which is quicker.
#   NFSHP2_SHOTS    frames saved to the card folder as shot_NNNNN.ppm:
#                   "every:N" or a list of frame numbers
#   NFSHP2_GLDEBUG  every N frames, what the draws did (c3d_gles.c)
#
# llvmpipe runs single-threaded (LP_NUM_THREADS=0): under QEMU its threaded
# rasterizer leaves what is drawn into a texture unseen by a later sample
# of it (only the clear shows), which a 40-line GLES program reproduces;
# softpipe and single-threaded llvmpipe draw it right. Device drivers are
# not involved.
set -e
here=$(cd "$(dirname "$0")" && (pwd -W 2>/dev/null || pwd))
card=$1
secs=${2:-240}
ARCH=${ARCH:-armhf}
if [ "$ARCH" = armhf ]; then
    BUILD_IMAGE=${BUILD_IMAGE:-nfshp2-r36s-build-armhf}
    tools=arm-linux-gnueabihf
    if [ -n "${NFSHP2_GL:-}" ]; then
        IMAGE=${IMAGE:-nfshp2-r36s-run-armhf}
    else
        IMAGE=${IMAGE:-$BUILD_IMAGE}
    fi
else
    BUILD_IMAGE=${BUILD_IMAGE:-takedawn-r36s-build:debian10}
    IMAGE=${IMAGE:-$BUILD_IMAGE}
    tools=aarch64-linux-gnu
fi
envs=
for v in $(env | sed -n 's/^\(NFSHP2_[A-Z0-9_]*\)=.*/\1/p'); do
    envs="$envs -e $v"
done
bin=/r36s/build/$ARCH/nfshp2.$ARCH
log=/r36s/build/$ARCH/run.log
MSYS_NO_PATHCONV=1 docker run --rm $envs -v "$here:/r36s" -v "$card:/card" $IMAGE sh -c "
  cd /card
  rm -f nfshp2_stderr.txt
  ${GALLIUM_DRIVER:+GALLIUM_DRIVER=$GALLIUM_DRIVER} ${LP_NUM_THREADS:+LP_NUM_THREADS=$LP_NUM_THREADS} SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH=/opt/r36s/lib \
    timeout $secs $bin > $log 2>&1
  echo EXIT=\$? >> $log
  if [ -f nfshp2_stderr.txt ]; then
    echo '--- nfshp2_stderr.txt' >> $log
    cat nfshp2_stderr.txt >> $log
  fi"
MSYS_NO_PATHCONV=1 docker run --rm -v "$here:/r36s" $BUILD_IMAGE sh -c "
  addrs=\$(grep -o 'pc [0-9a-f]* lr [0-9a-f]*' $log | sed 's/pc //;s/lr //')
  bt=\$(grep -o 'nfshp2.$ARCH[^[]*.0x[0-9a-f]*' $log | grep -o '0x[0-9a-f]*\$')
  if [ -n \"\$addrs\$bt\" ]; then
    echo '--- addr2line (pc, lr, then the backtrace)' >> $log
    for a in \$addrs \$bt; do
      printf '%s  ' \$a >> $log
      $tools-addr2line -f -i -p -e $bin \$a >> $log
    done
  fi"
tail -n 80 "$(dirname "$0")/build/$ARCH/run.log"
