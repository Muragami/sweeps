/*
	sweeps.h

	Simple audio resampler
	
	muragami, muragami@wishray.com, Jason A. Petrasko 2024
	MIT license: https://opensource.org/license/mit/
*/

// define this if you don't want to use or have the mwav header
#ifndef SWEEPS_NO_MWAV
#include "mwav.h"
#else
#include <stdint.h>
#endif

typedef struct _swsBuffer16 {
	int16_t *samples;
	int32_t size;
	int32_t freq;
	int32_t channels;
} swsBuffer16;

typedef struct _swsBufferF {
	float *samples;
	int32_t size;
	int32_t freq;
	int32_t channels;
} swsBufferF;

void swsResample16(swsBuffer16 in, swsBuffer16* out, int32_t freq, void* (*xmalloc)(size_t));
void swsResampleF(swsBufferF in, swsBufferF* out, int32_t freq, void* (*xmalloc)(size_t));

#ifndef SWEEPS_NO_MWAV

void swsResampleSnd(wavSound *in, wavSound* out, int32_t freq, void* (*xmalloc)(size_t));
void swsConvertSnd(wavSound *in, wavSound* out, int32_t bits, void* (*xmalloc)(size_t));

#endif