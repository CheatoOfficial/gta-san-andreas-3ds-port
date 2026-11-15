/* New 3DS output adapter for the PS2RC IOP sound-driver model. */
#ifndef PS2RC_AUDIO_3DS_H
#define PS2RC_AUDIO_3DS_H

#include <stdint.h>
#include "snd_driver.h"

typedef struct Audio3ds Audio3ds;

Audio3ds *audio_3ds_create(void (*log)(void *user, int level, const char *fmt, ...), void *log_user);
int audio_3ds_rpc(Audio3ds *audio, uint32_t fno, const void *send, uint32_t ssize,
                  void *recv, uint32_t rsize, uint64_t now_us);
SndDrv *audio_3ds_model(Audio3ds *audio);
void audio_3ds_report(Audio3ds *audio);
void audio_3ds_destroy(Audio3ds *audio);

#endif
