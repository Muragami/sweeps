/*
	sweeps.c

	Simple audio resampler
	
	muragami, muragami@wishray.com, Jason A. Petrasko 2024
	MIT license: https://opensource.org/license/mit/
*/

#include "sweeps.h"
#include <math.h>
#include <memory.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI   3.14159265358979323846
#endif

#ifndef M_1_PI
#define	M_1_PI 0.31830988618379067154
#endif

#define CHARSCALE				(1.0f / 128.0f)

#define SIDELOBE_HEIGHT 		96
#define UP_TRANSITION_WIDTH 	(1.0 / 32.0)
#define DOWN_TRANSITION_WIDTH 	(1.0 / 128.0)
#define MAX_SINC_WINDOW_SIZE 	2048
#define RESAMPLE_LUT_STEP 		128

typedef struct {
	float value;
	float delta;
} lutEntry_t;

lutEntry_t dynamicLut[RESAMPLE_LUT_STEP * MAX_SINC_WINDOW_SIZE];

static inline uint32_t calc_gcd(uint32_t a, uint32_t b) {
	while (b) {
		uint32_t t = b;
		b = a % b;
		a = t;
	}

	return a;
}

static inline double exact_nsinc(double x) {
	if (x == 0.0)
		return 1.0;

	return ((double)(M_1_PI) / x) * sin(M_PI * x);
}

// Modified Bessel function of the first kind, order 0
// https://ccrma.stanford.edu/~jos/sasp/Kaiser_Window.html
static inline double I0(double x) {
	double r = 1.0, xx = x * x, xpow = xx, coeff = 0.25;
	int32_t k;

	// iterations until coeff ~= 0
	// 19 for float32, 89 for float64, 880 for float80
	for (k = 1; k < 89; k++)
	{
		r += xpow * coeff;
		coeff /= (4 * k + 8) * k + 4;
		xpow *= xx;
	}

	return r;
}

// https://ccrma.stanford.edu/~jos/sasp/Kaiser_Window.html
static inline double kaiser(int32_t n, int32_t length, double beta) {
	double mid = 2 * n / (double)(length - 1) - 1.0;

	return I0(beta * sqrt(1.0 - mid * mid)) / I0(beta);
}

static inline void sinc_resample_createLut(int32_t inFreq, int32_t cutoffFreq2, int32_t windowSize, double beta) {
	double windowLut[windowSize];
	double freqAdjust = (double)cutoffFreq2 / (double)inFreq;
	lutEntry_t *out, *in;
	int32_t i, j;

	for (i = 0; i < windowSize; i++)
		windowLut[i] = kaiser(i, windowSize, beta);

	out = dynamicLut;
	for (i = 0; i < RESAMPLE_LUT_STEP; i++)
	{
		double offset = i / (double)(RESAMPLE_LUT_STEP - 1) - windowSize / 2;
		double sum = 0.0;
		for (j = 0; j < windowSize; j++)
		{
			double s = exact_nsinc((j + offset) * freqAdjust);
			out->value = s * windowLut[j];
			sum += s;
			out++;
		}

		out -= windowSize;
		for (j = 0; j < windowSize; j++)
		{
			out->value /= sum;
			out++;
		}
	}

	out = dynamicLut;
	in = out + windowSize;
	for (i = 0; i < RESAMPLE_LUT_STEP - 1; i++)
	{
		for (j = 0; j < windowSize; j++)
		{
			out->delta = in->value - out->value;
			out++;
			in++;
		}
	}

	for (j = 0; j < windowSize; j++)
	{
		out->delta = 0;
		out++;
	}
}

#define CUINT8TOF(x)	((float)x - 128.0f)
static inline void sinc_resample8_internal(uint8_t *wavOut, int32_t sizeOut, int32_t outFreq, 
						const uint8_t *wavIn, int32_t sizeIn, int32_t inFreq, int32_t cutoffFreq2,
						int32_t numChannels, int32_t windowSize, double beta) {
	float y[windowSize * numChannels];
	const uint8_t *sampleIn, *wavInEnd = wavIn + sizeIn;
	uint8_t *sampleOut, *wavOutEnd = wavOut + sizeOut;
	float outPeriod;
	int subpos = 0;
	int gcd = calc_gcd(inFreq, outFreq);
	int i, c, next;
	float dither[numChannels];

	sinc_resample_createLut(inFreq, cutoffFreq2, windowSize, beta);

	inFreq /= gcd;
	outFreq /= gcd;
	outPeriod = 1.0f / outFreq;

	for (c = 0; c < numChannels; c++)
		dither[c] = 0.0f;

	for (i = 0; i < windowSize / 2 - 1; i++)
	{
		for (c = 0; c < numChannels; c++)
			y[i * numChannels + c] = 0;
	}

	sampleIn = wavIn;
	for (; i < windowSize; i++)
	{
		for (c = 0; c < numChannels; c++) {
			float v = CUINT8TOF(*sampleIn);
			y[i * numChannels + c] = (sampleIn++ < wavInEnd) ? v : 0;
		}
	}

	sampleOut = wavOut;
	next = 0;
	while (sampleOut < wavOutEnd)
	{
		float samples[numChannels];
		float offset = 1.0f - subpos * outPeriod;
		float interp;
		lutEntry_t *lutPart;
		int index;

		for (c = 0; c < numChannels; c++)
			samples[c] = 0.0f;

		interp = offset * (RESAMPLE_LUT_STEP - 1);
		index = interp;
		interp -= index;
		lutPart = dynamicLut + index * windowSize;

		for (i = next; i < windowSize; i++, lutPart++)
		{
			float scale = lutPart->value + lutPart->delta * interp;

			for (c = 0; c < numChannels; c++)
				samples[c] += y[i * numChannels + c] * scale;
		}

		for (i = 0; i < next; i++, lutPart++)
		{
			float scale = lutPart->value + lutPart->delta * interp;

			for (c = 0; c < numChannels; c++)
				samples[c] += y[i * numChannels + c] * scale;
		}

		for (c = 0; c < numChannels; c++)
		{
			float r = roundf(samples[c] + dither[c]);
			dither[c] += samples[c] - r;

			if (r > 127)
				*sampleOut++ = 255;
			else if (r < -128)
				*sampleOut++ = 0;
			else
				*sampleOut++ = (uint8_t)(r + 128);
		}

		subpos += inFreq;
		while (subpos >= outFreq)
		{
			subpos -= outFreq;

			for (c = 0; c < numChannels; c++) {
				float v = CUINT8TOF(*sampleIn);
				y[next * numChannels + c] = (sampleIn++ < wavInEnd) ? v : 0;
			}

			next = (next + 1) % windowSize;
		}
	}
}

static inline void sinc_resample16_internal(int16_t *wavOut, int32_t sizeOut, int32_t outFreq, 
						const int16_t *wavIn, int32_t sizeIn, int32_t inFreq, int32_t cutoffFreq2,
						int32_t numChannels, int32_t windowSize, double beta) {
	float y[windowSize * numChannels];
	const int16_t *sampleIn, *wavInEnd = wavIn + (sizeIn / 2);
	int16_t *sampleOut, *wavOutEnd = wavOut + (sizeOut / 2);
	float outPeriod;
	int subpos = 0;
	int gcd = calc_gcd(inFreq, outFreq);
	int i, c, next;
	float dither[numChannels];

	sinc_resample_createLut(inFreq, cutoffFreq2, windowSize, beta);

	inFreq /= gcd;
	outFreq /= gcd;
	outPeriod = 1.0f / outFreq;

	for (c = 0; c < numChannels; c++)
		dither[c] = 0.0f;

	for (i = 0; i < windowSize / 2 - 1; i++)
	{
		for (c = 0; c < numChannels; c++)
			y[i * numChannels + c] = 0;
	}

	sampleIn = wavIn;
	for (; i < windowSize; i++)
	{
		for (c = 0; c < numChannels; c++)
			y[i * numChannels + c] = (sampleIn < wavInEnd) ? *sampleIn++ : 0;
	}

	sampleOut = wavOut;
	next = 0;
	while (sampleOut < wavOutEnd)
	{
		float samples[numChannels];
		float offset = 1.0f - subpos * outPeriod;
		float interp;
		lutEntry_t *lutPart;
		int index;

		for (c = 0; c < numChannels; c++)
			samples[c] = 0.0f;

		interp = offset * (RESAMPLE_LUT_STEP - 1);
		index = interp;
		interp -= index;
		lutPart = dynamicLut + index * windowSize;

		for (i = next; i < windowSize; i++, lutPart++)
		{
			float scale = lutPart->value + lutPart->delta * interp;

			for (c = 0; c < numChannels; c++)
				samples[c] += y[i * numChannels + c] * scale;
		}

		for (i = 0; i < next; i++, lutPart++)
		{
			float scale = lutPart->value + lutPart->delta * interp;

			for (c = 0; c < numChannels; c++)
				samples[c] += y[i * numChannels + c] * scale;
		}

		for (c = 0; c < numChannels; c++)
		{
			float r = roundf(samples[c] + dither[c]);
			dither[c] += samples[c] - r;

			if (r > 32767)
				*sampleOut++ = 32767;
			else if (r < -32768)
				*sampleOut++ = -32768;
			else
				*sampleOut++ = r;
		}

		subpos += inFreq;
		while (subpos >= outFreq)
		{
			subpos -= outFreq;

			for (c = 0; c < numChannels; c++)
				y[next * numChannels + c] = (sampleIn < wavInEnd) ? *sampleIn++ : 0;

			next = (next + 1) % windowSize;
		}
	}
}

static inline void sinc_resampleF_internal(float *wavOut, int32_t sizeOut, int32_t outFreq, 
						const float *wavIn, int32_t sizeIn, int32_t inFreq, int32_t cutoffFreq2,
						int32_t numChannels, int32_t windowSize, double beta) {
	float y[windowSize * numChannels];
	const float *sampleIn, *wavInEnd = wavIn + (sizeIn / 4);
	float *sampleOut, *wavOutEnd = wavOut + (sizeOut / 4);
	float outPeriod;
	int subpos = 0;
	int gcd = calc_gcd(inFreq, outFreq);
	int i, c, next;

	sinc_resample_createLut(inFreq, cutoffFreq2, windowSize, beta);

	inFreq /= gcd;
	outFreq /= gcd;
	outPeriod = 1.0f / outFreq;

	for (i = 0; i < windowSize / 2 - 1; i++)
	{
		for (c = 0; c < numChannels; c++)
			y[i * numChannels + c] = 0;
	}

	sampleIn = wavIn;
	for (; i < windowSize; i++)
	{
		for (c = 0; c < numChannels; c++)
			y[i * numChannels + c] = (sampleIn < wavInEnd) ? *sampleIn++ : 0;
	}

	sampleOut = wavOut;
	next = 0;
	while (sampleOut < wavOutEnd)
	{
		float samples[numChannels];
		float offset = 1.0f - subpos * outPeriod;
		float interp;
		lutEntry_t *lutPart;
		int index;

		for (c = 0; c < numChannels; c++)
			samples[c] = 0.0f;

		interp = offset * (RESAMPLE_LUT_STEP - 1);
		index = interp;
		interp -= index;
		lutPart = dynamicLut + index * windowSize;

		for (i = next; i < windowSize; i++, lutPart++)
		{
			float scale = lutPart->value + lutPart->delta * interp;

			for (c = 0; c < numChannels; c++)
				samples[c] += y[i * numChannels + c] * scale;
		}

		for (i = 0; i < next; i++, lutPart++)
		{
			float scale = lutPart->value + lutPart->delta * interp;

			for (c = 0; c < numChannels; c++)
				samples[c] += y[i * numChannels + c] * scale;
		}

		for (c = 0; c < numChannels; c++)
		{
			float r = samples[c];

			if (r > 1.0f)
				*sampleOut++ = 1.0f;
			else if (r < -1.0f)
				*sampleOut++ = -1.0f;
			else
				*sampleOut++ = r;
		}

		subpos += inFreq;
		while (subpos >= outFreq)
		{
			subpos -= outFreq;

			for (c = 0; c < numChannels; c++)
				y[next * numChannels + c] = (sampleIn < wavInEnd) ? *sampleIn++ : 0;

			next = (next + 1) % windowSize;
		}
	}
}

void sinc_resample8(uint8_t *wavOut, int32_t sizeOut, int32_t outFreq, const uint8_t *wavIn,
						int32_t sizeIn, int32_t inFreq, int32_t numChannels) {
	double sidelobeHeight = SIDELOBE_HEIGHT;
	double transitionWidth;
	double beta = 0.0;
	int32_t cutoffFreq2;
	int32_t windowSize;

	// Just copy if no resampling necessary
	if (outFreq == inFreq)
	{
		memcpy(wavOut, wavIn, (sizeOut < sizeIn) ? sizeOut : sizeIn);
		return;
	}

	transitionWidth = (outFreq > inFreq) ? UP_TRANSITION_WIDTH : DOWN_TRANSITION_WIDTH;

	// cutoff freq is ideally half transition width away from output freq
	cutoffFreq2 = outFreq - transitionWidth * inFreq * 0.5;

	// FIXME: Figure out why there are bad effects with cutoffFreq2 > inFreq
	if (cutoffFreq2 > inFreq)
		cutoffFreq2 = inFreq;

	// https://www.mathworks.com/help/signal/ug/kaiser-window.html
	if (sidelobeHeight > 50)
		beta = 0.1102 * (sidelobeHeight - 8.7);
	else if (sidelobeHeight >= 21)
		beta = 0.5842 * pow(sidelobeHeight - 21.0, 0.4) + 0.07886 * (sidelobeHeight - 21.0);

	windowSize = (sidelobeHeight - 8.0) / (2.285 * transitionWidth * M_PI) + 1;

	if (windowSize > MAX_SINC_WINDOW_SIZE)
		windowSize = MAX_SINC_WINDOW_SIZE;

	// should compile as different paths
	// number of channels need to be compiled as separate paths to ensure good
	// vectorization by the compiler
	if (numChannels == 1)
		sinc_resample8_internal(wavOut, sizeOut, outFreq, wavIn, sizeIn, inFreq, cutoffFreq2, 1, windowSize, beta);
	else if (numChannels == 2)
		sinc_resample8_internal(wavOut, sizeOut, outFreq, wavIn, sizeIn, inFreq, cutoffFreq2, 2, windowSize, beta);
	else
		sinc_resample8_internal(wavOut, sizeOut, outFreq, wavIn, sizeIn, inFreq, cutoffFreq2, numChannels, windowSize, beta);
}

void sinc_resample16(int16_t *wavOut, int32_t sizeOut, int32_t outFreq, const int16_t *wavIn,
						int32_t sizeIn, int32_t inFreq, int32_t numChannels) {
	double sidelobeHeight = SIDELOBE_HEIGHT;
	double transitionWidth;
	double beta = 0.0;
	int32_t cutoffFreq2;
	int32_t windowSize;

	// Just copy if no resampling necessary
	if (outFreq == inFreq)
	{
		memcpy(wavOut, wavIn, (sizeOut < sizeIn) ? sizeOut : sizeIn);
		return;
	}

	transitionWidth = (outFreq > inFreq) ? UP_TRANSITION_WIDTH : DOWN_TRANSITION_WIDTH;

	// cutoff freq is ideally half transition width away from output freq
	cutoffFreq2 = outFreq - transitionWidth * inFreq * 0.5;

	// FIXME: Figure out why there are bad effects with cutoffFreq2 > inFreq
	if (cutoffFreq2 > inFreq)
		cutoffFreq2 = inFreq;

	// https://www.mathworks.com/help/signal/ug/kaiser-window.html
	if (sidelobeHeight > 50)
		beta = 0.1102 * (sidelobeHeight - 8.7);
	else if (sidelobeHeight >= 21)
		beta = 0.5842 * pow(sidelobeHeight - 21.0, 0.4) + 0.07886 * (sidelobeHeight - 21.0);

	windowSize = (sidelobeHeight - 8.0) / (2.285 * transitionWidth * M_PI) + 1;

	if (windowSize > MAX_SINC_WINDOW_SIZE)
		windowSize = MAX_SINC_WINDOW_SIZE;

	// should compile as different paths
	// number of channels need to be compiled as separate paths to ensure good
	// vectorization by the compiler
	if (numChannels == 1)
		sinc_resample16_internal(wavOut, sizeOut, outFreq, wavIn, sizeIn, inFreq, cutoffFreq2, 1, windowSize, beta);
	else if (numChannels == 2)
		sinc_resample16_internal(wavOut, sizeOut, outFreq, wavIn, sizeIn, inFreq, cutoffFreq2, 2, windowSize, beta);
	else
		sinc_resample16_internal(wavOut, sizeOut, outFreq, wavIn, sizeIn, inFreq, cutoffFreq2, numChannels, windowSize, beta);
}

void sinc_resampleF(float *wavOut, int32_t sizeOut, int32_t outFreq, const float *wavIn,
					int32_t sizeIn, int32_t inFreq, int32_t numChannels) {
	double sidelobeHeight = SIDELOBE_HEIGHT;
	double transitionWidth;
	double beta = 0.0;
	int32_t cutoffFreq2;
	int32_t windowSize;

	// Just copy if no resampling necessary
	if (outFreq == inFreq)
	{
		memcpy(wavOut, wavIn, (sizeOut < sizeIn) ? sizeOut : sizeIn);
		return;
	}

	transitionWidth = (outFreq > inFreq) ? UP_TRANSITION_WIDTH : DOWN_TRANSITION_WIDTH;

	// cutoff freq is ideally half transition width away from output freq
	cutoffFreq2 = outFreq - transitionWidth * inFreq * 0.5;

	// FIXME: Figure out why there are bad effects with cutoffFreq2 > inFreq
	if (cutoffFreq2 > inFreq)
		cutoffFreq2 = inFreq;

	// https://www.mathworks.com/help/signal/ug/kaiser-window.html
	if (sidelobeHeight > 50)
		beta = 0.1102 * (sidelobeHeight - 8.7);
	else if (sidelobeHeight >= 21)
		beta = 0.5842 * pow(sidelobeHeight - 21.0, 0.4) + 0.07886 * (sidelobeHeight - 21.0);

	windowSize = (sidelobeHeight - 8.0) / (2.285 * transitionWidth * M_PI) + 1;

	if (windowSize > MAX_SINC_WINDOW_SIZE)
		windowSize = MAX_SINC_WINDOW_SIZE;

	// should compile as different paths
	// number of channels need to be compiled as separate paths to ensure good
	// vectorization by the compiler
	if (numChannels == 1)
		sinc_resampleF_internal(wavOut, sizeOut, outFreq, wavIn, sizeIn, inFreq, cutoffFreq2, 1, windowSize, beta);
	else if (numChannels == 2)
		sinc_resampleF_internal(wavOut, sizeOut, outFreq, wavIn, sizeIn, inFreq, cutoffFreq2, 2, windowSize, beta);
	else
		sinc_resampleF_internal(wavOut, sizeOut, outFreq, wavIn, sizeIn, inFreq, cutoffFreq2, numChannels, windowSize, beta);	
}

void swsResampleSnd8(wavSound *in, wavSound* out, int32_t freq, xmalloc xm) {
	int32_t gcd = calc_gcd(freq, in->sampleRate);

	if (xm == NULL) xm = malloc;

	out->data.numBytes = in->data.numBytes * (int64_t)(freq / gcd) / (int64_t)(in->sampleRate / gcd);
	out->data.bytes = xm(out->data.numBytes);
	if (out->data.bytes == NULL) wavFatal("swsResampleSnd8() allocation failure");
	out->sampleRate = freq;
	out->channels = in->channels;
	out->bitsPerSample = 8;

	sinc_resample8((uint8_t*)out->data.bytes, out->data.numBytes, out->sampleRate, (uint8_t*)in->data.bytes, 
						in->data.numBytes, in->sampleRate, in->channels);
}

void swsResampleSnd16(wavSound *in, wavSound* out, int32_t freq, xmalloc xm) {
	int32_t gcd = calc_gcd(freq, in->sampleRate);

	if (xm == NULL) xm = malloc;

	out->data.numBytes = in->data.numBytes * (int64_t)(freq / gcd) / (int64_t)(in->sampleRate / gcd);
	out->data.bytes = xm(out->data.numBytes);
	if (out->data.bytes == NULL) wavFatal("swsResampleSnd16() allocation failure");
	out->sampleRate = freq;
	out->channels = in->channels;
	out->bitsPerSample = 16;

	sinc_resample16((int16_t*)out->data.bytes, out->data.numBytes, out->sampleRate, (int16_t*)in->data.bytes, 
						in->data.numBytes, in->sampleRate, in->channels);
}

void swsResampleSndF(wavSound *in, wavSound* out, int32_t freq, xmalloc xm) {
	int32_t gcd = calc_gcd(freq, in->sampleRate);

	if (xm == NULL) xm = malloc;

	out->data.numBytes = in->data.numBytes * (int64_t)(freq / gcd) / (int64_t)(in->sampleRate / gcd);
	out->data.bytes = xm(out->data.numBytes);
	if (out->data.bytes == NULL) wavFatal("swsResampleSndF() allocation failure");
	out->sampleRate = freq;
	out->channels = in->channels;
	out->bitsPerSample = in->bitsPerSample;

	sinc_resampleF((float*)out->data.bytes, out->data.numBytes, out->sampleRate, (float*)in->data.bytes, 
						in->data.numBytes, in->sampleRate, in->channels);
}

void swsResampleSnd(wavSound *in, wavSound* out, int32_t freq, xmalloc xm) {
	switch (in->bitsPerSample) {
		case 32:
		case 24:
			swsResampleSndF(in, out, freq, xm);
			break;
		case 16:
			swsResampleSnd16(in, out, freq, xm);
			break;
		case 8:
			swsResampleSnd8(in, out, freq, xm);
			break;
	}
}

void swsConvertSndF(wavSound *in, wavSound* out, int32_t bits, xmalloc xm) {
	int32_t samples = (in->data.numBytes >> 2) / in->channels;
	int32_t c = in->channels;
	float *inf = (float*)in->data.bytes;
	int16_t *o16;
	uint8_t *o8;
	switch (bits) {
		case 8:
			out->bitsPerSample = 8;
			out->data.numBytes = in->data.numBytes >> 2;
			out->data.bytes = xm(out->data.numBytes);
			if (out->data.bytes == NULL) wavFatal("swsConvertSndF() allocation failure (8)");
			samples *= c;
			o8 = (uint8_t*)out->data.bytes;
			while (samples--) *o8++ = (uint8_t)(((*inf++) + 1.0f) * 127.5f); 
			break;
		case 16:
			out->bitsPerSample = 16;
			out->data.numBytes = in->data.numBytes >> 1;
			out->data.bytes = xm(out->data.numBytes);
			if (out->data.bytes == NULL) wavFatal("swsConvertSndF() allocation failure (16)");
			o16 = (int16_t*)out->data.bytes;
			samples *= c;
			while (samples--) *o16++ = (int16_t)((*inf++) * 32767.0f);
			break;
		case 24:
		case 32:
			out->bitsPerSample = bits;
			out->data.numBytes = in->data.numBytes;
			out->data.bytes = xm(out->data.numBytes);
			if (out->data.bytes == NULL) wavFatal("swsConvertSndF() allocation failure (F)");
			memcpy(out->data.bytes, in->data.bytes, in->data.numBytes);
			break;
	}
	out->sampleRate = in->sampleRate;
	out->channels = in->channels;
}

void swsConvertSnd16(wavSound *in, wavSound* out, int32_t bits, xmalloc xm) {
	int32_t samples = (in->data.numBytes >> 1) / in->channels;
	int32_t c = in->channels;
	int16_t *in16 = (int16_t*)in->data.bytes;
	uint8_t *o8;
	float *oF;
	switch (bits) {
		case 8:
			out->bitsPerSample = 8;
			out->data.numBytes = in->data.numBytes >> 1;
			out->data.bytes = xm(out->data.numBytes);
			if (out->data.bytes == NULL) wavFatal("swsConvertSnd16() allocation failure (8)");
			o8 = (uint8_t*)out->data.bytes;
			samples *= c;
			while (samples--) *o8++ = (((float)(*in16++) / 256.0f) + 128.0f);	
			break;
		case 16:
			out->bitsPerSample = 16;
			out->data.numBytes = in->data.numBytes;
			out->data.bytes = xm(out->data.numBytes);
			if (out->data.bytes == NULL) wavFatal("swsConvertSnd16() allocation failure (16)");
			memcpy(out->data.bytes, in->data.bytes, in->data.numBytes);
			break;
		case 24:
		case 32:
			out->bitsPerSample = bits;
			out->data.numBytes = in->data.numBytes << 1;
			out->data.bytes = xm(out->data.numBytes);
			if (out->data.bytes == NULL) wavFatal("swsConvertSnd16() allocation failure (F)");
			samples *= c;
			oF = (float*)out->data.bytes;
			while (samples--) *oF++ = (float)(*in16++) / 32768.0F; 			
			break;
	}
	out->sampleRate = in->sampleRate;
	out->channels = in->channels;		
}

void swsConvertSnd8(wavSound *in, wavSound* out, int32_t bits, xmalloc xm) {
	int32_t samples = in->data.numBytes / in->channels;
	int32_t c = in->channels;
	uint8_t *in8 = in->data.bytes;
	int16_t *o16;
	float *oF;
	switch (bits) {
		case 8:
			out->bitsPerSample = 8;
			out->data.numBytes = in->data.numBytes;
			out->data.bytes = xm(out->data.numBytes);
			if (out->data.bytes == NULL) wavFatal("swsConvertSnd8() allocation failure (8)");
			memcpy(out->data.bytes, in->data.bytes, in->data.numBytes);
			break;
		case 16:
			out->bitsPerSample = 16;
			out->data.numBytes = in->data.numBytes << 1;
			out->data.bytes = xm(out->data.numBytes);
			if (out->data.bytes == NULL) wavFatal("swsConvertSnd8() allocation failure (16)");
			o16 = (int16_t*)out->data.bytes;
			samples *= c;
			while (samples--) *o16++ = (int16_t)(((float)(*in8++) - 128.0f) * 256.0f);
			break;
		case 24:
		case 32:
			out->bitsPerSample = bits;
			out->data.numBytes = in->data.numBytes << 2;
			out->data.bytes = xm(out->data.numBytes);
			if (out->data.bytes == NULL) wavFatal("swsConvertSnd8() allocation failure (F)");
			samples *= c;
			oF = (float*)out->data.bytes;
			while (samples--) *oF++ = ((float)(*in8++) / 127.0f) - 1.0f; 			
			break;
	}
	out->sampleRate = in->sampleRate;
	out->channels = in->channels;	
}

void swsConvertSnd(wavSound *in, wavSound* out, int32_t bits, xmalloc xm) {
	if (xm == NULL) xm = malloc;
	if (in->bitsPerSample == 32 || in->bitsPerSample == 24)
		swsConvertSndF(in, out, bits, xm);
	else if (in->bitsPerSample == 16)
		swsConvertSnd16(in, out, bits, xm);
	else if (in->bitsPerSample == 8)
		swsConvertSnd8(in, out, bits, xm);
}

