/*
	main.c

	command line .wav file resampler
	
	muragami, muragami@wishray.com, Jason A. Petrasko 2024
	MIT license: https://opensource.org/license/mit/
*/


#include "main.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

double getTime() {
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 0.000000001;
}


int main(int argc, const char **argv) {
	wavSound wIn;
	wavSound wOut;
	int32_t freq;
	double samples;
	double start;
	double stop;
	double len;
	const char *e;

	if (argc == 4) {
		e = wavLoadFile(argv[1], &wIn, NULL);
		if (e != NULL) {
			printf("error loading '%s': %s", argv[1], e);
			return -1;
		}
		freq = atoi(argv[3]);
		if (freq < 8000) {
			printf("invalid frequency: %s", argv[3]);
			return -1;
		}
		if (!(wIn.bitsPerSample == 16 || wIn.bitsPerSample == 32)) {
			printf("invalid input bitdepth: %d", wIn.bitsPerSample);
			return -1;
		}
		samples = (double)wIn.data.numBytes / (double)(wIn.channels * (wIn.bitsPerSample >> 3));
		len = samples / (double)wIn.sampleRate;
		printf("converting %.0f samples (%.2f seconds).\n", samples, len);
		start = getTime();
		swsResampleSnd(&wIn, &wOut, freq, NULL);
		stop = getTime();
		printf("complete.\n");
		printf("conversion from %d[%d] to %d[%d] in %.2g seconds.\n", wIn.sampleRate, wIn.channels, wOut.sampleRate, wOut.channels, stop - start);
		printf("\t%.2fx realtime.\n", len / (stop - start));
		e = wavSaveFile(argv[2], &wOut);
		if (e != NULL) {
			printf("error writing '%s': %s", argv[2], e);
			return -1;
		}
		
	} else if (argc == 5) {
		// TODO
	} else {
		printf("usage:\n");
		printf("\tsweeps <wave_file> <out_file> <new_freq>\n");
		printf("\tsweeps <wave_file> <out_file> <new_freq> <new_bits>\n");
	}
	return 0;
}