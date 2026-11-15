/* New 3DS NDSP sink for PS2RC's portable PS2 IOP sound-driver and SPU2-lite model. */
#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include "audio_3ds.h"
#include "hle.h"
#include "spu2_lite.h"

#define AUDIO_3DS_CHANNEL 0
#define AUDIO_3DS_BUFFERS 16
#define AUDIO_3DS_FRAMES  256

struct Audio3ds {
    SndDrv driver;
    Spu2 spu;
    uint8_t *ram;
    int16_t *pcm;
    ndspWaveBuf wave[AUDIO_3DS_BUFFERS];
    uint64_t last_us;
    uint64_t queued_frames;
    uint64_t dropped_frames;
    uint64_t disc_errors;
    int started;
    int ndsp_ready;
    int state_only;
    void (*log)(void *user, int level, const char *fmt, ...);
    void *log_user;
};

static void *audio_alloc(void *user, size_t bytes)
{
    (void)user;
    return calloc(1, bytes);
}

static int disc_size(void *user, const char *path, uint32_t *size_out)
{
    Audio3ds *a = (Audio3ds *)user;
    if (hle_disc_open(path, size_out) >= 0) return SNDD_IO_OK;
    a->disc_errors++;
    return SNDD_IO_ERROR;
}

static int disc_read(void *user, const char *path, uint32_t offset, void *dst, uint32_t bytes)
{
    Audio3ds *a = (Audio3ds *)user;
    uint32_t size = 0;
    int fd = hle_disc_open(path, &size);
    if (fd < 0 || offset > size || bytes > size - offset ||
        hle_disc_read_fd(fd, offset, dst, bytes) != (int64_t)bytes) {
        a->disc_errors++;
        return SNDD_IO_ERROR;
    }
    return SNDD_IO_OK;
}

static void ndsp_sink(void *user, const int16_t *stereo48, uint32_t frames)
{
    Audio3ds *a = (Audio3ds *)user;
    if (!a->ndsp_ready) {
        a->dropped_frames += frames;
        return;
    }
    uint32_t pos = 0;
    while (pos < frames) {
        int slot = -1;
        for (int i = 0; i < AUDIO_3DS_BUFFERS; ++i) {
            if (a->wave[i].status == NDSP_WBUF_FREE || a->wave[i].status == NDSP_WBUF_DONE) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            a->dropped_frames += frames - pos;
            return;
        }
        uint32_t n = frames - pos;
        if (n > AUDIO_3DS_FRAMES) n = AUDIO_3DS_FRAMES;
        ndspWaveBuf *wb = &a->wave[slot];
        int16_t *dst = a->pcm + (size_t)slot * AUDIO_3DS_FRAMES * 2;
        memcpy(dst, stereo48 + (size_t)pos * 2, (size_t)n * 4);
        DSP_FlushDataCache(dst, n * 4);
        memset(wb, 0, sizeof *wb);
        wb->data_pcm16 = dst;
        wb->nsamples = n * 2; /* NDSP counts interleaved PCM16 halfwords, not stereo frames. */
        ndspChnWaveBufAdd(AUDIO_3DS_CHANNEL, wb);
        a->queued_frames += n;
        pos += n;
    }
}

Audio3ds *audio_3ds_create(void (*log)(void *user, int level, const char *fmt, ...), void *log_user)
{
    Audio3ds *a = (Audio3ds *)calloc(1, sizeof *a);
    if (!a) return NULL;
    a->log = log;
    a->log_user = log_user;
    a->ram = (uint8_t *)calloc(1, SPU2_RAM_SIZE);
    if (!a->ram) goto fail;

    Result result = ndspInit();
    if (R_FAILED(result)) {
        if (a->log) a->log(a->log_user, 0,
                           "NDSP init unavailable (0x%08lx); IOP/SPU2 model will run without output",
                           (unsigned long)result);
    } else {
        a->ndsp_ready = 1;
        a->pcm = (int16_t *)linearAlloc((size_t)AUDIO_3DS_BUFFERS * AUDIO_3DS_FRAMES * 4);
        if (!a->pcm) {
            a->ndsp_ready = 0;
            ndspExit();
            if (a->log) a->log(a->log_user, 0,
                               "NDSP PCM queue allocation failed; IOP/SPU2 model will run without output");
        } else {
            memset(a->pcm, 0, (size_t)AUDIO_3DS_BUFFERS * AUDIO_3DS_FRAMES * 4);
            ndspSetOutputMode(NDSP_OUTPUT_STEREO);
            ndspSetMasterVol(0.8f);
            ndspChnReset(AUDIO_3DS_CHANNEL);
            ndspChnSetFormat(AUDIO_3DS_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
            ndspChnSetRate(AUDIO_3DS_CHANNEL, (float)SPU2_RATE);
            ndspChnSetInterp(AUDIO_3DS_CHANNEL, NDSP_INTERP_LINEAR);
            float mix[12] = {1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            ndspChnSetMix(AUDIO_3DS_CHANNEL, mix);
        }
    }

    spu2_init(&a->spu, a->ram);
    /* Azahar currently cannot provide NDSP. Preserve IOP-visible voice timing and
       registers without spending emulator time decoding/mixing samples that cannot
       be heard. A New 3DS with NDSP keeps the complete audio path. */
    if (!a->ndsp_ready) {
        a->spu.x_state_only = 1;
        a->state_only = 1;
        if (a->log) a->log(a->log_user, 0,
                           "PS2 IOP audio running SPU2 state-only (NDSP output unavailable)");
    }
    a->spu.sink = a->ndsp_ready ? ndsp_sink : NULL;
    a->spu.sink_user = a;
    SnddConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.io.user = a;
    cfg.io.size = disc_size;
    cfg.io.read = disc_read;
    cfg.alloc = audio_alloc;
    cfg.log = log;
    cfg.log_user = log_user;
    spu2_bind_sndd(&a->spu, &cfg.spu, &a->driver, 1);
    sndd_init(&a->driver, &cfg);
    if (a->log) a->log(a->log_user, 1, a->ndsp_ready ?
                       "PS2 IOP audio model connected to NDSP at %u Hz" :
                       "PS2 IOP audio model initialized without PCM output", SPU2_RATE);
    return a;

fail:
    if (a->ndsp_ready) ndspExit();
    if (a->pcm) linearFree(a->pcm);
    free(a->ram);
    free(a);
    return NULL;
}

int audio_3ds_rpc(Audio3ds *a, uint32_t fno, const void *send, uint32_t ssize,
                  void *recv, uint32_t rsize, uint64_t now_us)
{
    uint32_t ms = 0;
    if (fno == 1) {
        if (!a->started) {
            a->started = 1;
            a->last_us = now_us;
        }
        uint64_t delta = now_us > a->last_us ? now_us - a->last_us : 0;
        if (delta > 10000000u) delta = 10000000u;
        ms = (uint32_t)(delta / 1000u);
        a->last_us += (uint64_t)ms * 1000u;
        if (now_us > a->last_us + 10000000u) a->last_us = now_us;
    }
    return sndd_rpc(&a->driver, fno, (const uint8_t *)send, ssize, (uint8_t *)recv, rsize, ms);
}

SndDrv *audio_3ds_model(Audio3ds *a) { return a ? &a->driver : NULL; }

void audio_3ds_report(Audio3ds *a)
{
    if (!a || !a->log) return;
    a->log(a->log_user, 1,
           "NDSP audio: mode %s, queued %llu frames, dropped %llu frames, disc read errors %llu",
           a->state_only ? "state-only" : (a->ndsp_ready ? "output" : "unavailable"),
           (unsigned long long)a->queued_frames, (unsigned long long)a->dropped_frames,
           (unsigned long long)a->disc_errors);
}

void audio_3ds_destroy(Audio3ds *a)
{
    if (!a) return;
    audio_3ds_report(a);
    if (a->ndsp_ready) {
        ndspChnWaveBufClear(AUDIO_3DS_CHANNEL);
        ndspChnReset(AUDIO_3DS_CHANNEL);
        ndspExit();
    }
    if (a->pcm) linearFree(a->pcm);
    free(a->ram);
    free(a);
}
