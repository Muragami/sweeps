/*
	sweeps.h

	Simple audio resampler
	
	muragami, muragami@wishray.com, Jason A. Petrasko 2024
	MIT license: https://opensource.org/license/mit/
*/

// define this if you don't want to use or have the mwav header
#ifndef SWEEPS_NO_MWAV
	// we are using mwav, so just include that
	#include "mwav.h"
#else
	// no mwav, so define the basic structs we need
	#include <stdint.h>

	typedef struct _wavData {
		uint8_t *bytes;
		size_t numBytes;
	} wavData;

	typedef struct _wavSound {
		wavData data;
		int32_t channels;
		int32_t sampleRate;
		int32_t bitsPerSample;
	} wavSound;

	typedef void* (*xmalloc)(size_t x);
#endif

void swsResampleSnd(wavSound *in, wavSound* out, int32_t freq, xmalloc xm);
void swsConvertSnd(wavSound *in, wavSound* out, int32_t bits, xmalloc xm);

