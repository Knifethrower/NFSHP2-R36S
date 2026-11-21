// Sound straight to the kernel's ALSA PCM device, without libasound.
//
// For systems whose 32-bit libasound can't open any device: one built with
// 64-bit time_t asks the kernel for the time64 PCM calls, which kernels
// before 5.6 lack ("Inappropriate ioctl for device"). This build's headers
// have the 32-bit time_t layout that every 64-bit kernel's compat layer
// takes, and the calls used here (hw and sw params, prepare, write) carry no
// times at all.
#ifndef ALSA_RAW_H
#define ALSA_RAW_H

#include <stdint.h>

typedef void (*alsa_raw_fill)(void* user, uint8_t* stream, int len);

// Opens the first playback PCM in /dev/snd as interleaved S16 stereo at
// `rate` (or 48000, 44100 or 32000 when that rate isn't there), and calls
// `fill` for each period on a thread of its own, under alsa_raw_lock.
// Returns 0 and the rate and period got, or -1 after logging why.
int  alsa_raw_open(int rate, alsa_raw_fill fill, void* user, int* gotRate, int* gotPeriod,
                   char* name, int nameSize);
void alsa_raw_close(void);
void alsa_raw_lock(void);
void alsa_raw_unlock(void);

#endif
