// libctru's NDSP as a software mixer on SDL's audio device.
//
// The 3DS's DSP plays 24 channels of PCM8, PCM16 or DSP-ADPCM from chains
// of wave buffers, each channel at its own rate, mixed by a 12-gain matrix
// (front, back, and two aux busses). Here an SDL callback does the same at
// the device's rate: each channel walks its chain, decoding ADPCM a sample
// ahead of a linear-interpolated read position, and adds front + back of
// its gains into the stereo out. A buffer goes QUEUED, PLAYING, DONE as on
// the DSP, which is what the game's streamers wait on.

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "alsa_raw.h"

#define NDSP_NUM_CHANNELS 24
#define OUT_RATE 48000

typedef struct {
    u16          format;
    float        rate;
    float        mix[12];
    s16          coefs[16];
    bool         paused;
    ndspWaveBuf* head;          // the chain, first is playing
    u16          seq;
    double       pos;           // read position in the head buffer, samples
    // ADPCM: the next sample index to decode, and the history.
    u32          decNext;
    s16          hist1, hist2;
    // The last two decoded frames of each channel (L/R), for interpolation.
    s16          prev[2], cur[2];
    u32          curIndex;      // the sample `cur` holds, +1 (0: none yet)
} Channel;

static Channel        s_ch[NDSP_NUM_CHANNELS];
static SDL_AudioDeviceID s_dev;
static float          s_master = 1.0f;
static ndspOutputMode s_mode = NDSP_OUTPUT_STEREO;
static bool           s_up;
static bool           s_raw;            // playing through alsa_raw.c, not SDL
static double         s_outRate = OUT_RATE;

static void lock(void)
{
    if (s_dev) SDL_LockAudioDevice(s_dev);
    else if (s_raw) alsa_raw_lock();
}

static void unlock(void)
{
    if (s_dev) SDL_UnlockAudioDevice(s_dev);
    else if (s_raw) alsa_raw_unlock();
}

static int clamp16(int v)
{
    return v < -32768 ? -32768 : v > 32767 ? 32767 : v;
}

static int channels_of(u16 format) { return (format & 3) == 2 ? 2 : 1; }
static int encoding_of(u16 format) { return (format >> 2) & 3; }

static void start_buffer(Channel* c, ndspWaveBuf* w)
{
    c->pos = (double)w->offset;
    c->decNext = 0;
    c->curIndex = 0;
    c->prev[0] = c->prev[1] = c->cur[0] = c->cur[1] = 0;
    if (w->adpcm_data != NULL) {
        c->hist1 = w->adpcm_data->history0;
        c->hist2 = w->adpcm_data->history1;
    }
    w->status = NDSP_WBUF_PLAYING;
}

// DSP-ADPCM: 8-byte frames of a header (coefficient pair << 4 | scale)
// and 14 4-bit samples, high nibble first.
static s16 adpcm_next(Channel* c, const u8* data)
{
    u32 i = c->decNext++;
    u32 frame = i / 14, k = i % 14;
    u8 ps = data[frame * 8];
    u8 byte = data[frame * 8 + 1 + k / 2];
    int nib = (k & 1) ? (byte & 0xF) : (byte >> 4);
    int scale = 1 << (ps & 0xF);
    int idx = (ps >> 4) & 7;
    int c1 = c->coefs[idx * 2], c2 = c->coefs[idx * 2 + 1];
    int s;
    if (nib >= 8) nib -= 16;
    s = (nib * scale) << 11;
    s = (s + 1024 + c1 * c->hist1 + c2 * c->hist2) >> 11;
    s = clamp16(s);
    c->hist2 = c->hist1;
    c->hist1 = (s16)s;
    return (s16)s;
}

// Sample `index` of the head buffer into out[0..1].
static void fetch(Channel* c, const ndspWaveBuf* w, u32 index, s16 out[2])
{
    int enc = encoding_of(c->format), nch = channels_of(c->format);
    if (enc == NDSP_ENCODING_ADPCM) {
        s16 s = 0;
        if (c->decNext > index + 1) {   // a rewind (a loop): start over
            c->decNext = 0;
            if (w->adpcm_data) {
                c->hist1 = w->adpcm_data->history0;
                c->hist2 = w->adpcm_data->history1;
            } else {
                c->hist1 = c->hist2 = 0;
            }
        }
        while (c->decNext <= index)
            s = adpcm_next(c, w->data_adpcm);
        out[0] = out[1] = s;
    } else if (enc == NDSP_ENCODING_PCM16) {
        out[0] = w->data_pcm16[index * nch];
        out[1] = w->data_pcm16[index * nch + (nch - 1)];
    } else {
        out[0] = (s16)(w->data_pcm8[index * nch] << 8);
        out[1] = (s16)(w->data_pcm8[index * nch + (nch - 1)] << 8);
    }
}

static void finish_head(Channel* c)
{
    ndspWaveBuf* w = c->head;
    c->head = w->next;
    w->status = NDSP_WBUF_DONE;
    if (c->head != NULL) start_buffer(c, c->head);
}

static void mix_channel(Channel* c, float* out, int frames)
{
    double step = c->rate / s_outRate;
    float gl = c->mix[0] + c->mix[2], gr = c->mix[1] + c->mix[3];
    bool stereo = channels_of(c->format) == 2;

    for (int f = 0; f < frames && c->head != NULL; f++) {
        ndspWaveBuf* w = c->head;
        u32 i = (u32)c->pos;
        float t = (float)(c->pos - i), l, r;

        if (i >= w->nsamples) {
            if (w->looping) {
                c->pos -= w->nsamples;
                i = (u32)c->pos;
                c->curIndex = 0;
            } else {
                finish_head(c);
                f--;
                continue;
            }
        }
        if (c->curIndex != i + 1) {
            if (c->curIndex == i) {          // moved on by one: shift
                c->prev[0] = c->cur[0];
                c->prev[1] = c->cur[1];
            } else {
                fetch(c, w, i > 0 ? i - 1 : 0, c->prev);
            }
            fetch(c, w, i, c->cur);
            c->curIndex = i + 1;
        }
        // Interpolate between sample i and i+1 by reading i+1 lazily: the
        // pair held is (i-1, i), so lean back by one sample.
        l = c->prev[0] + (c->cur[0] - c->prev[0]) * t;
        r = c->prev[1] + (c->cur[1] - c->prev[1]) * t;
        if (!stereo) r = l;
        out[f * 2] += l * gl;
        out[f * 2 + 1] += r * gr;
        c->pos += step;
    }
}

static void SDLCALL callback(void* user, Uint8* stream, int len)
{
    int frames = len / (int)(2 * sizeof(s16));
    float buf[4096 * 2];
    s16* out = (s16*)stream;
    (void)user;

    while (frames > 0) {
        int n = frames > 4096 ? 4096 : frames;
        memset(buf, 0, sizeof(float) * 2 * (size_t)n);
        for (int ch = 0; ch < NDSP_NUM_CHANNELS; ch++) {
            Channel* c = &s_ch[ch];
            if (!c->paused && c->head != NULL && c->rate > 0.0f)
                mix_channel(c, buf, n);
        }
        for (int i = 0; i < n; i++) {
            float l = buf[i * 2] * s_master, r = buf[i * 2 + 1] * s_master;
            if (s_mode == NDSP_OUTPUT_MONO) l = r = (l + r) * 0.5f;
            out[i * 2] = (s16)clamp16((int)lrintf(l));
            out[i * 2 + 1] = (s16)clamp16((int)lrintf(r));
        }
        out += n * 2;
        frames -= n;
    }
}

static void reset_channel(Channel* c)
{
    ndspWaveBuf* w;
    for (w = c->head; w != NULL; w = w->next)
        w->status = NDSP_WBUF_DONE;
    memset(c, 0, sizeof *c);
    c->format = NDSP_FORMAT_MONO_PCM16;
    c->rate = 32728.0f;
    c->mix[0] = c->mix[1] = 1.0f;
}

// One try: SDL's audio up (with the driver SDL_AUDIODRIVER names, or SDL's
// own pick) and its default device open. The reason for a failure is logged,
// since a silent game otherwise says nothing about why.
static bool try_open(const char* what)
{
    SDL_AudioSpec want, have;
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("Sound: %s: no audio driver (%s)\n", what, SDL_GetError());
        return false;
    }
    memset(&want, 0, sizeof want);
    want.freq = OUT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = callback;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_dev == 0) {
        const char* drv = SDL_GetCurrentAudioDriver();
        printf("Sound: %s: %s can't open a device (%s)\n", what,
               drv != NULL ? drv : "no driver", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    printf("Sound: %s audio, %d Hz, %d channels, %d-sample buffer%s%s\n",
           SDL_GetCurrentAudioDriver(), have.freq, have.channels, have.samples,
           getenv("AUDIODEV") != NULL ? ", device " : "",
           getenv("AUDIODEV") != NULL ? getenv("AUDIODEV") : "");
    return true;
}

// When the default fails: the device may still be held for a moment by what
// ran before (PortMaster's installer, the frontend's music), so try again a
// few times; then every other driver SDL has, and on ALSA the sound card
// itself in case the system's "default" names a plugin this 32-bit build
// can't load. NFSHP2_AUDIO_FALLBACK=0 keeps only the first try. A driver or
// device the player names with SDL_AUDIODRIVER or AUDIODEV is left alone.
static bool open_with_fallbacks(void)
{
    static const char* const kAlsaDevices[] = { "sysdefault", "plughw:0,0", "hw:0,0" };
    const char* off = getenv("NFSHP2_AUDIO_FALLBACK");
    bool userDriver = getenv("SDL_AUDIODRIVER") != NULL;
    bool userDevice = getenv("AUDIODEV") != NULL;
    char what[48];

    if (try_open("default")) return true;
    if (off != NULL && off[0] == '0') return false;
    for (int i = 2; i <= 4; i++) {
        SDL_Delay(250);
        snprintf(what, sizeof what, "default, try %d", i);
        if (try_open(what)) return true;
    }
    for (int d = 0; !userDriver && d < SDL_GetNumAudioDrivers(); d++) {
        const char* name = SDL_GetAudioDriver(d);
        if (name == NULL || strcmp(name, "dummy") == 0 || strcmp(name, "disk") == 0)
            continue;
        setenv("SDL_AUDIODRIVER", name, 1);
        if (try_open(name)) return true;
        if (strcmp(name, "alsa") != 0 || userDevice) continue;
        for (size_t k = 0; k < sizeof kAlsaDevices / sizeof kAlsaDevices[0]; k++) {
            setenv("AUDIODEV", kAlsaDevices[k], 1);
            snprintf(what, sizeof what, "alsa %s", kAlsaDevices[k]);
            if (try_open(what)) return true;
        }
        unsetenv("AUDIODEV");
    }
    if (!userDriver) unsetenv("SDL_AUDIODRIVER");
    return false;
}

static void raw_fill(void* user, uint8_t* stream, int len)
{
    callback(user, stream, len);
}

// The last resort, and NFSHP2_AUDIO=raw's only way: the sound card through
// the kernel directly (alsa_raw.h), for a system whose 32-bit libasound
// opens nothing.
static bool open_raw(void)
{
    int rate = 0, period = 0;
    char name[48];
    if (alsa_raw_open(OUT_RATE, raw_fill, NULL, &rate, &period, name, sizeof name) != 0)
        return false;
    s_outRate = rate;
    s_raw = true;
    printf("Sound: raw ALSA on %s, %d Hz, 2 channels, %d-frame periods\n", name, rate, period);
    return true;
}

Result ndspInit(void)
{
    const char* mode = getenv("NFSHP2_AUDIO");
    const char* off = getenv("NFSHP2_AUDIO_FALLBACK");
    if (s_up) return 0;
    for (int i = 0; i < NDSP_NUM_CHANNELS; i++)
        reset_channel(&s_ch[i]);
    if (mode != NULL && strcmp(mode, "raw") == 0) {
        if (!open_raw()) return R36S_RESULT_UNSUPPORTED;
    } else if (open_with_fallbacks()) {
        SDL_PauseAudioDevice(s_dev, 0);
    } else if ((off != NULL && off[0] == '0') || !open_raw()) {
        return R36S_RESULT_UNSUPPORTED;
    }
    s_up = true;
    return 0;
}

void ndspExit(void)
{
    if (!s_up) return;
    if (s_raw) {
        alsa_raw_close();
        s_raw = false;
    } else {
        SDL_CloseAudioDevice(s_dev);
        s_dev = 0;
    }
    s_up = false;
}

void ndspSetOutputMode(ndspOutputMode mode) { s_mode = mode; }
void ndspSetMasterVol(float volume) { s_master = volume; }

#define CH(id) if ((unsigned)(id) >= NDSP_NUM_CHANNELS) return
#define CHV(id, v) if ((unsigned)(id) >= NDSP_NUM_CHANNELS) return (v)

void ndspChnReset(int id)
{
    CH(id);
    lock();
    reset_channel(&s_ch[id]);
    unlock();
}

void ndspChnSetFormat(int id, u16 format) { CH(id); lock(); s_ch[id].format = format; unlock(); }
void ndspChnSetInterp(int id, ndspInterpType type) { (void)id; (void)type; }
void ndspChnSetRate(int id, float rate) { CH(id); lock(); s_ch[id].rate = rate; unlock(); }

void ndspChnSetMix(int id, float mix[12])
{
    CH(id);
    lock();
    memcpy(s_ch[id].mix, mix, sizeof s_ch[id].mix);
    unlock();
}

void ndspChnSetAdpcmCoefs(int id, u16 coefs[16])
{
    CH(id);
    lock();
    memcpy(s_ch[id].coefs, coefs, sizeof s_ch[id].coefs);
    unlock();
}

void ndspChnSetPaused(int id, bool paused) { CH(id); s_ch[id].paused = paused; }
bool ndspChnIsPaused(int id) { CHV(id, false); return s_ch[id].paused; }
bool ndspChnIsPlaying(int id) { CHV(id, false); return s_ch[id].head != NULL; }

u32 ndspChnGetSamplePos(int id)
{
    CHV(id, 0);
    return (u32)s_ch[id].pos;
}

u16 ndspChnGetWaveBufSeq(int id)
{
    CHV(id, 0);
    return s_ch[id].head ? s_ch[id].head->sequence_id : 0;
}

void ndspChnWaveBufClear(int id)
{
    ndspWaveBuf* w;
    CH(id);
    lock();
    for (w = s_ch[id].head; w != NULL; w = w->next)
        w->status = NDSP_WBUF_DONE;
    s_ch[id].head = NULL;
    s_ch[id].pos = 0;
    unlock();
}

void ndspChnWaveBufAdd(int id, ndspWaveBuf* buf)
{
    Channel* c;
    CH(id);
    if (buf == NULL || buf->nsamples == 0) return;
    c = &s_ch[id];
    lock();
    buf->next = NULL;
    buf->sequence_id = ++c->seq;
    buf->status = NDSP_WBUF_QUEUED;
    if (c->head == NULL) {
        c->head = buf;
        start_buffer(c, buf);
    } else {
        ndspWaveBuf* t = c->head;
        while (t->next != NULL) t = t->next;
        t->next = buf;
    }
    unlock();
}
