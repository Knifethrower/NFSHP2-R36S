// Sound straight to the kernel's ALSA PCM device (alsa_raw.h says why).
//
// The steps are tinyalsa's: hw params (interleaved, S16_LE, 2 channels, the
// rate, a period near 1024 frames and 4 of them, the kernel choosing the
// rest), sw params (start once a period is queued, stop only on underrun),
// prepare, then blocking writes of a period at a time. An underrun or a
// suspend prepares the device again and carries on.

#define _GNU_SOURCE
#include "alsa_raw.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <sound/asound.h>

// The sizes a 64-bit kernel's compat layer takes from 32-bit programs
// (sound/core/pcm_compat.c): anything else would be another ioctl number.
#if defined(__arm__)
_Static_assert(sizeof(struct snd_pcm_hw_params) == 604, "hw params layout");
_Static_assert(sizeof(struct snd_pcm_sw_params) == 104, "sw params layout");
_Static_assert(sizeof(struct snd_xferi) == 12, "xferi layout");
#endif

static int             s_fd = -1;
static pthread_t       s_thread;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int    s_run;
static alsa_raw_fill   s_fill;
static void*           s_user;
static int             s_period;

void alsa_raw_lock(void) { pthread_mutex_lock(&s_mutex); }
void alsa_raw_unlock(void) { pthread_mutex_unlock(&s_mutex); }

static struct snd_mask* mask_of(struct snd_pcm_hw_params* p, int param)
{
    return &p->masks[param - SNDRV_PCM_HW_PARAM_FIRST_MASK];
}

static struct snd_interval* interval_of(struct snd_pcm_hw_params* p, int param)
{
    return &p->intervals[param - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
}

static void params_any(struct snd_pcm_hw_params* p)
{
    memset(p, 0, sizeof *p);
    for (int k = SNDRV_PCM_HW_PARAM_FIRST_MASK; k <= SNDRV_PCM_HW_PARAM_LAST_MASK; k++)
        memset(mask_of(p, k)->bits, 0xff, sizeof mask_of(p, k)->bits);
    for (int k = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; k <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; k++) {
        interval_of(p, k)->min = 0;
        interval_of(p, k)->max = UINT_MAX;
    }
    p->rmask = ~0u;
    p->info = ~0u;
}

static void mask_set(struct snd_pcm_hw_params* p, int param, unsigned v)
{
    struct snd_mask* m = mask_of(p, param);
    memset(m->bits, 0, sizeof m->bits);
    m->bits[v >> 5] |= 1u << (v & 31);
}

static void interval_set(struct snd_pcm_hw_params* p, int param, unsigned lo, unsigned hi)
{
    struct snd_interval* i = interval_of(p, param);
    i->min = lo;
    i->max = hi;
    i->integer = 1;
}

static int set_hw(int fd, int rate, int exactPeriod, struct snd_pcm_hw_params* p)
{
    params_any(p);
    mask_set(p, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    mask_set(p, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
    mask_set(p, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    interval_set(p, SNDRV_PCM_HW_PARAM_CHANNELS, 2, 2);
    interval_set(p, SNDRV_PCM_HW_PARAM_RATE, (unsigned)rate, (unsigned)rate);
    if (exactPeriod) {
        interval_set(p, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 1024, 1024);
        interval_set(p, SNDRV_PCM_HW_PARAM_PERIODS, 4, 4);
    } else {
        interval_set(p, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 256, 4096);
        interval_set(p, SNDRV_PCM_HW_PARAM_PERIODS, 2, 16);
    }
    return ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, p);
}

// The playback PCMs in /dev/snd (pcmC<card>D<device>p), lowest first.
static int list_pcms(char out[][32], int max)
{
    DIR* d = opendir("/dev/snd");
    struct dirent* e;
    int n = 0;
    if (d == NULL) return 0;
    while ((e = readdir(d)) != NULL && n < max) {
        int card, dev;
        char p;
        if (sscanf(e->d_name, "pcmC%dD%d%c", &card, &dev, &p) == 3 && p == 'p')
            snprintf(out[n++], 32, "%.31s", e->d_name);
    }
    closedir(d);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(out[j - 1], out[j]) > 0; j--) {
            char t[32];
            memcpy(t, out[j], 32);
            memcpy(out[j], out[j - 1], 32);
            memcpy(out[j - 1], t, 32);
        }
    return n;
}

static int recover(int fd, int err)
{
    if (err == ESTRPIPE) {
        struct timespec ts = { 0, 10 * 1000 * 1000 };
        for (int i = 0; i < 100 && ioctl(fd, SNDRV_PCM_IOCTL_RESUME) < 0 && errno == EAGAIN; i++)
            nanosleep(&ts, NULL);
    }
    return ioctl(fd, SNDRV_PCM_IOCTL_PREPARE);
}

static void* writer(void* arg)
{
    size_t bytes = (size_t)s_period * 4;
    uint8_t* buf = malloc(bytes);
    int failures = 0;
    (void)arg;
    if (buf == NULL) return NULL;
    while (s_run) {
        struct snd_xferi x;
        alsa_raw_lock();
        s_fill(s_user, buf, (int)bytes);
        alsa_raw_unlock();
        x.result = 0;
        x.buf = buf;
        x.frames = (snd_pcm_uframes_t)s_period;
        if (ioctl(s_fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
            int err = errno;
            if (err == EPIPE || err == ESTRPIPE) {
                recover(s_fd, err);
            } else if (++failures == 1 || failures % 500 == 0) {
                printf("Sound: raw ALSA write failed (%s)\n", strerror(err));
                usleep(10000);
            } else {
                usleep(10000);
            }
        } else {
            failures = 0;
        }
    }
    free(buf);
    return NULL;
}

int alsa_raw_open(int rate, alsa_raw_fill fill, void* user, int* gotRate, int* gotPeriod,
                  char* name, int nameSize)
{
    static const int kRates[] = { 0, 48000, 44100, 32000 };
    char pcms[16][32];
    int n = list_pcms(pcms, 16);

    if (n == 0) {
        printf("Sound: raw ALSA: no playback device in /dev/snd\n");
        return -1;
    }
    for (int i = 0; i < n; i++) {
        char path[48];
        struct snd_pcm_hw_params hw;
        struct snd_pcm_sw_params sw;
        int fd, ok = -1;

        snprintf(path, sizeof path, "/dev/snd/%s", pcms[i]);
        fd = open(path, O_WRONLY | O_CLOEXEC);
        if (fd < 0) {
            printf("Sound: raw ALSA: %s won't open (%s)\n", path, strerror(errno));
            continue;
        }
        for (size_t r = 0; ok < 0 && r < sizeof kRates / sizeof kRates[0]; r++) {
            int want = r == 0 ? rate : kRates[r];
            if (r > 0 && want == rate) continue;
            ok = set_hw(fd, want, 1, &hw);
            if (ok < 0) ok = set_hw(fd, want, 0, &hw);
        }
        if (ok < 0) {
            printf("Sound: raw ALSA: %s takes no S16 stereo format (%s)\n", path, strerror(errno));
            close(fd);
            continue;
        }
        s_period = (int)interval_of(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE)->min;
        {
            unsigned buffer = interval_of(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE)->min;
            unsigned long boundary = buffer;
            memset(&sw, 0, sizeof sw);
            sw.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
            sw.period_step = 1;
            sw.avail_min = (snd_pcm_uframes_t)s_period;
            sw.start_threshold = (snd_pcm_uframes_t)s_period;
            sw.stop_threshold = buffer;
            while (boundary * 2 <= (unsigned long)INT_MAX - buffer) boundary *= 2;
            sw.boundary = boundary;
            if (ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) < 0 ||
                ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0) {
                printf("Sound: raw ALSA: %s won't start (%s)\n", path, strerror(errno));
                close(fd);
                continue;
            }
        }
        *gotRate = (int)interval_of(&hw, SNDRV_PCM_HW_PARAM_RATE)->min;
        *gotPeriod = s_period;
        snprintf(name, (size_t)nameSize, "%s", path);
        s_fd = fd;
        s_fill = fill;
        s_user = user;
        s_run = 1;
        if (pthread_create(&s_thread, NULL, writer, NULL) != 0) {
            printf("Sound: raw ALSA: no thread\n");
            s_run = 0;
            close(fd);
            s_fd = -1;
            return -1;
        }
        return 0;
    }
    return -1;
}

void alsa_raw_close(void)
{
    if (s_fd < 0) return;
    s_run = 0;
    pthread_join(s_thread, NULL);
    ioctl(s_fd, SNDRV_PCM_IOCTL_DROP);
    close(s_fd);
    s_fd = -1;
}
