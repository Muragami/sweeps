/*
	mwav.h

	Very basic .wav file load and save, supports:
		* stdio
		* memory blocks
		* PHYSFS (if WAV_USE_PHYSFS is defined before inclusion)
	
	only accepts/handles:
		8, 16, 24^, and 32^ bit PCM only

		^internally 24 and 32 bit is turned into normalized IEEE float
		- this means some small data loss on 32 bit PCM input FWIW

	muragami, muragami@wishray.com, Jason A. Petrasko 2024
	MIT license: https://opensource.org/license/mit/
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define WAV_USE_PHYSFS

#ifdef WAV_USE_PHYSFS
#include <physfs.h>
#endif

#define WRITE_FOURCC(dst, src) 	((dst)[0] = (src)[0], (dst)[1] = (src)[1],\
								(dst)[2] = (src)[2], (dst)[3] = (src)[3])
#define MATCH_FOURCC(dst, src) 	((dst)[0] == (src)[0] && (dst)[1] == (src)[1]\
								 && (dst)[2] == (src)[2] && (dst)[3] == (src)[3])
//#define WAV_FAIL(x)				{ err = x; goto ferr; }
#define WAV_FAILS(x)			{ err = x; goto serr; }

// ******************************************************************************
// structs

typedef struct __attribute__((__packed__)) _wavChunkHeader {
	char id[4];
	uint32_t size;
} wavChunkHeader;

typedef struct __attribute__((__packed__)) _wavFmtData {
	uint16_t formatTag;
	uint16_t channels;
	uint32_t sampleRate;
	uint32_t avgBytesPerSec;
	uint16_t blockAlign;
	uint16_t bitsPerSample;
} wavFmtData;

typedef struct __attribute__((__packed__)) _wavSaveHeader {
	wavChunkHeader riffHeader;
	char id_WAVE[4];
	wavChunkHeader fmt_Header;
	wavFmtData fmt_Data;
	wavChunkHeader dataHeader;
} wavSaveHeader;

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

#define CBUFFER_CNT 	(1024)
#define CBUFFER_BYTES 	(1024 * 3)
#define CBUFFER_MBYTES 	(1024 * 4)
typedef struct _wavConvertBuffer {
	uint8_t c[CBUFFER_MBYTES];
} wavConvertBuffer;
typedef struct _wavConvertBuffer32 {
	int32_t c[CBUFFER_CNT];
} wavConvertBuffer32;

typedef void* (*xmalloc)(size_t x);

typedef struct _wavVirtualIO {
	void *user;
	int64_t (*write)(void*, void*, uint64_t);
	int64_t (*read)(void*, void*, uint64_t);
	int64_t (*tell)(void*);
	int64_t (*seek)(void*, int64_t);
} wavVirtualIO;

// ******************************************************************************
// Virtualized IO stuff

static int64_t __attribute__((unused)) wavio_fread(void *f, void *buffer, uint64_t bytes) {
	if (f == NULL) return 0;
	return fread(buffer, 1, bytes, (FILE*)f);
}

static int64_t __attribute__((unused)) wavio_fwrite(void *f, void *buffer, uint64_t bytes) {
	if (f == NULL) return 0;
	return fwrite(buffer, 1, bytes, (FILE*)f);
}

static int64_t __attribute__((unused)) wavio_ftell(void *f) {
	if (f == NULL) return 0;
	return ftell((FILE*)f);
}

static int64_t __attribute__((unused)) wavio_fseek(void *f, int64_t bytes) {
	if (f == NULL) return 0;
	return fseek((FILE*)f, bytes, SEEK_SET);
}

static void __attribute__((unused)) wavioFileOpenRead(wavVirtualIO *io, const char *fname) {
	io->user = fopen(fname, "rb");
	io->read = wavio_fread;
	io->write = wavio_fwrite;
	io->tell = wavio_ftell;
	io->seek = wavio_fseek;
}

static void __attribute__((unused)) wavioFileOpenWrite(wavVirtualIO *io, const char *fname) {
	io->user = fopen(fname, "wb");
	io->read = wavio_fread;
	io->write = wavio_fwrite;
	io->tell = wavio_ftell;
	io->seek = wavio_fseek;
}

static void __attribute__((unused)) wavioFileClose(wavVirtualIO *io) {
	if (io->user != NULL) fclose((FILE*)io->user);
}

#ifdef WAV_USE_PHYSFS

static int64_t __attribute__((unused)) wavio_pread(void *f, void *buffer, uint64_t bytes) {
	if (f == NULL) return 0;
	return PHYSFS_readBytes((PHYSFS_file*)f, buffer, bytes);
}

static int64_t __attribute__((unused)) wavio_pwrite(void *f, void *buffer, uint64_t bytes) {
	if (f == NULL) return 0;
	return PHYSFS_writeBytes((PHYSFS_file*)f, buffer, bytes);
}

static int64_t __attribute__((unused)) wavio_ptell(void *f) {
	if (f == NULL) return 0;
	return PHYSFS_tell((PHYSFS_file*)f);
}

static int64_t __attribute__((unused)) wavio_pseek(void *f, int64_t bytes) {
	if (f == NULL) return 0;
	return PHYSFS_seek((PHYSFS_file*)f, bytes);
}

static void __attribute__((unused)) wavioPhysFSOpenRead(wavVirtualIO *io, const char *fname) {
	io->user = PHYSFS_openRead(fname);
	io->read = wavio_fread;
	io->write = wavio_fwrite;
	io->tell = wavio_ftell;
	io->seek = wavio_fseek;
}

static void __attribute__((unused)) wavioPhysFSOpenWrite(wavVirtualIO *io, const char *fname) {
	io->user = PHYSFS_openWrite(fname);
	io->read = wavio_fread;
	io->write = wavio_fwrite;
	io->tell = wavio_ftell;
	io->seek = wavio_fseek;
}

static void __attribute__((unused)) wavioPhysFSClose(wavVirtualIO *io) {
	if (io->user != NULL) PHYSFS_close((PHYSFS_file*)io->user);
}

#endif

// ******************************************************************************
// File load and save

static const char* __attribute__((unused)) wavSaveFile(wavVirtualIO *io, wavSound *snd) {
	wavSaveHeader head;
	uint32_t zero = 0;
	const char *err = NULL;

	WRITE_FOURCC(head.riffHeader.id, "RIFF");
	head.riffHeader.size = snd->data.numBytes + sizeof(head) - sizeof(head.riffHeader);
	WRITE_FOURCC(head.id_WAVE, "WAVE");

	WRITE_FOURCC(head.fmt_Header.id, "fmt ");
	head.fmt_Header.size = 16;
	head.fmt_Data.formatTag = 1;
	head.fmt_Data.channels = snd->channels;
	head.fmt_Data.sampleRate = snd->sampleRate;
	head.fmt_Data.avgBytesPerSec = snd->sampleRate * snd->channels * snd->bitsPerSample / 8;
	head.fmt_Data.blockAlign = (snd->bitsPerSample >> 3) * snd->channels;
	head.fmt_Data.bitsPerSample = snd->bitsPerSample;

	WRITE_FOURCC(head.dataHeader.id, "data");
	if (snd->bitsPerSample == 24) 
		head.dataHeader.size = (uint64_t)snd->data.numBytes * 3ll / 4ll;
	 else 
	 	head.dataHeader.size = snd->data.numBytes;

	if (io->write(io->user, &head, sizeof(head)) != sizeof(head)) WAV_FAILS("Failed to write header")
	if (snd->bitsPerSample == 24) {
		wavConvertBuffer b;
		uint32_t samples = snd->data.numBytes >> 2;
		float *f = (float*)snd->data.bytes;
		while (samples > 0) {
			int32_t cnt;
			if (samples > CBUFFER_CNT) cnt = CBUFFER_CNT;
			 else cnt = samples;
			for (int i = 0; i < cnt; i++) {
				int32_t v = (*f * 2147483648.0f);
				b.c[i * 3] = (v & 0xFF00) >> 8;
				b.c[i * 3 + 1] = (v & 0xFF0000) >> 16;
				b.c[i * 3 + 2] = (v & 0xFF000000) >> 24;
				f++;
			}
			samples -= cnt;
			if (io->write(io->user, b.c, cnt * 3) != cnt * 3) WAV_FAILS("Failed to write data")
		}
		// uneven bytes written? add one null
		if ((snd->data.numBytes / (4 * snd->channels) * (snd->bitsPerSample >> 3) * snd->channels) % 2 > 0)
			io->write(io->user, &zero, 1);
	} else if (snd->bitsPerSample == 32) {
		wavConvertBuffer b;
		uint32_t samples = snd->data.numBytes >> 2;
		float *f = (float*)snd->data.bytes;
		while (samples > 0) {
			int32_t cnt;
			if (samples > CBUFFER_CNT) cnt = CBUFFER_CNT;
			 else cnt = samples;
			for (int i = 0; i < cnt; i++) {
				int32_t v = (*f * 2147483648.0f);
				memcpy(b.c + i * 4, &v, 4);
				f++;
			}
			samples -= cnt;
			if (io->write(io->user, b.c, cnt * 4) != cnt * 4) WAV_FAILS("Failed to write data")
		}
	} else {
		if (io->write(io->user, snd->data.bytes, snd->data.numBytes) != snd->data.numBytes) WAV_FAILS("Failed to write data")
	}

serr:
	return err;
}

static const char* __attribute__((unused)) wavLoadFile(wavVirtualIO *io, wavSound *snd, xmalloc xm) {
	wavChunkHeader chunkHeader;
	char waveId[4];
	const char *err = NULL;

	if (xm == NULL) xm = malloc;

	if (io->read(io->user, &chunkHeader, sizeof(chunkHeader)) != sizeof(chunkHeader)) WAV_FAILS("Failed to read RIFF header")
	if (!MATCH_FOURCC(chunkHeader.id, "RIFF")) WAV_FAILS("File is not RIFF")
	if (io->read(io->user, waveId, 4) != 4) WAV_FAILS("Failed to read WAVE header")
	if (!MATCH_FOURCC(waveId, "WAVE")) WAV_FAILS("File is not RIFF WAVE")

	while (1)
	{
		size_t endPos;
		if (io->read(io->user, &chunkHeader, sizeof(chunkHeader)) != sizeof(chunkHeader)) break;

		endPos = io->tell(io->user) + chunkHeader.size;

		if (MATCH_FOURCC(chunkHeader.id, "fmt ")) {
			wavFmtData fmtData;

			if (chunkHeader.size < sizeof(fmtData)) WAV_FAILS("Badly formatted 'fmt ' chunk")
			if (io->read(io->user, &fmtData, sizeof(fmtData)) != sizeof(fmtData)) WAV_FAILS("Failed to read 'fmt_' chunk")
			if (fmtData.formatTag != 1) WAV_FAILS("File is not PCM")
			if (!(fmtData.bitsPerSample == 16 || fmtData.bitsPerSample == 8 || fmtData.bitsPerSample == 24
				|| fmtData.bitsPerSample == 32)) WAV_FAILS("File is unsupported bits per sample.")
			snd->channels = fmtData.channels;
			snd->sampleRate = fmtData.sampleRate;
			snd->bitsPerSample = fmtData.bitsPerSample;
		} else if (MATCH_FOURCC(chunkHeader.id, "data"))
		{
			if (snd->bitsPerSample == 24) {
				snd->data.numBytes = (uint64_t)chunkHeader.size * 4ll / 3ll;
				snd->data.bytes = (uint8_t *)xm(snd->data.numBytes);
				float *f = (float*)snd->data.bytes;
				wavConvertBuffer b;
				while (chunkHeader.size > 0) {
					int cnt;
					if (chunkHeader.size > CBUFFER_BYTES) cnt = CBUFFER_BYTES;
					 else cnt = chunkHeader.size;
					if (io->read(io->user, b.c, cnt) != cnt) WAV_FAILS("Failed to read data.")
					for (int i = 0; i < cnt / 3; i++) {
						*f = ((b.c[i * 3] << 8) + (b.c[i * 3 + 1] << 16) + (b.c[i * 3 + 2] << 24)) / 2147483648.0f;
						f++;
					}
					chunkHeader.size -= cnt;
				}
			} else if (snd->bitsPerSample == 32) {
				snd->data.numBytes = chunkHeader.size;
				snd->data.bytes = (uint8_t *)xm(snd->data.numBytes);
				float *f = (float*)snd->data.bytes;
				wavConvertBuffer32 b;
				while (chunkHeader.size > 0) {
					int cnt;
					if (chunkHeader.size > CBUFFER_MBYTES) cnt = CBUFFER_MBYTES;
					 else cnt = chunkHeader.size;
					if (io->read(io->user, b.c, cnt) != cnt) WAV_FAILS("Failed to read data.")
					for (int i = 0; i < cnt / 4; i++) {
						*f = (float)b.c[i] / 2147483648.0f;
						f++;
					}
					chunkHeader.size -= cnt;
				}				
			} else {
				snd->data.bytes = (unsigned char *)xm(chunkHeader.size);
				chunkHeader.size = io->read(io->user, snd->data.bytes, chunkHeader.size);
				snd->data.numBytes = chunkHeader.size;	
			}
			
		}

		io->seek(io->user, endPos);
	}

serr:
	return err;
}

// ******************************************************************************
// Memory load and save

static const char* __attribute__((unused)) wavSaveMemory(const char *filename, wavSound *snd, wavData *out, xmalloc xm) {
	wavSaveHeader head;
	uint32_t dataSize;
	int32_t odd = (snd->data.numBytes / (4 * snd->channels) * (snd->bitsPerSample >> 3) * snd->channels) % 2 > 0;

	if (snd->bitsPerSample == 24) 
		dataSize = odd + (uint64_t)snd->data.numBytes * 3ll / 4ll;
	 else 
	 	dataSize = snd->data.numBytes;

	if (xm == NULL) xm = malloc;
	out->bytes = (uint8_t*)xm(sizeof(head) + dataSize);
	if (out->bytes == NULL) return "xmalloc() allocation failed";
	out->numBytes = sizeof(head) + snd->data.numBytes;

	WRITE_FOURCC(head.riffHeader.id, "RIFF");
	head.riffHeader.size = snd->data.numBytes + sizeof(head) - sizeof(head.riffHeader);
	WRITE_FOURCC(head.id_WAVE, "WAVE");

	WRITE_FOURCC(head.fmt_Header.id, "fmt ");
	head.fmt_Header.size = 16;
	head.fmt_Data.formatTag = 1;
	head.fmt_Data.channels = snd->channels;
	head.fmt_Data.sampleRate = snd->sampleRate;
	head.fmt_Data.avgBytesPerSec = snd->sampleRate * snd->channels * snd->bitsPerSample / 8;
	head.fmt_Data.blockAlign = (snd->bitsPerSample >> 3) * snd->channels;
	head.fmt_Data.bitsPerSample = snd->bitsPerSample;

	WRITE_FOURCC(head.dataHeader.id, "data");
	head.dataHeader.size = dataSize;
	
	memcpy(out->bytes, &head, sizeof(head));
	uint8_t *p = out->bytes + sizeof(head);
	if (snd->bitsPerSample == 24) {
		wavConvertBuffer b;
		uint32_t samples = snd->data.numBytes >> 2;
		float *f = (float*)snd->data.bytes;
		while (samples > 0) {
			int32_t cnt;
			if (samples > CBUFFER_CNT) cnt = CBUFFER_CNT;
			 else cnt = samples;
			for (int i = 0; i < cnt; i++) {
				int32_t v = (*f * 2147483648.0f);
				b.c[i * 3] = (v & 0xFF00) >> 8;
				b.c[i * 3 + 1] = (v & 0xFF0000) >> 16;
				b.c[i * 3 + 2] = (v & 0xFF000000) >> 24;
				f++;
			}
			samples -= cnt;
			memcpy(p, b.c, cnt * 3);
			p += cnt *3;
		}
		// uneven bytes written? add one null
		if (odd) *p = 0;
	} else if (snd->bitsPerSample == 32) {
		wavConvertBuffer b;
		uint32_t samples = snd->data.numBytes >> 2;
		float *f = (float*)snd->data.bytes;
		while (samples > 0) {
			int32_t cnt;
			if (samples > CBUFFER_CNT) cnt = CBUFFER_CNT;
			 else cnt = samples;
			for (int i = 0; i < cnt; i++) {
				int32_t v = (*f * 2147483648.0f);
				memcpy(b.c + i * 4, &v, 4);
				f++;
			}
			samples -= cnt;
			memcpy(p, b.c, cnt * 3);
			p += cnt *3;
		}
	} else {
		memcpy(p, snd->data.bytes, snd->data.numBytes);
	}
	
	return NULL;
}

static const char* __attribute__((unused)) wavLoadMemory(wavData *in, wavSound *snd, xmalloc xm) {
	wavChunkHeader* pHeader;
	int32_t mpos = 0;
	const char *err = NULL;

	if (xm == NULL) xm = malloc;

	pHeader = (wavChunkHeader*)in->bytes;
	if (mpos + sizeof(wavChunkHeader) > in->numBytes) WAV_FAILS("Failed to read RIFF header")
	mpos += sizeof(wavChunkHeader);
	if (!MATCH_FOURCC(pHeader->id, "RIFF")) WAV_FAILS("File is not RIFF")
	if (mpos + 4 > in->numBytes) WAV_FAILS("Failed to read WAVE header")
	if (!MATCH_FOURCC(in->bytes + mpos, "WAVE")) WAV_FAILS("File is not RIFF WAVE")
	mpos += 4;

	while (mpos < in->numBytes)
	{
		if (mpos + sizeof(wavChunkHeader) > in->numBytes) break;
		pHeader = (wavChunkHeader*)(in->bytes + mpos);
		mpos += sizeof(wavChunkHeader);

		if (MATCH_FOURCC(pHeader->id, "fmt ")) {
			wavFmtData* pFmt;

			if (pHeader->size < sizeof(wavFmtData)) WAV_FAILS("Badly formatted 'fmt ' chunk")
			if (mpos + sizeof(wavChunkHeader) > in->numBytes) WAV_FAILS("Failed to read 'fmt_' chunk")
			pFmt = (wavFmtData*)in->bytes + mpos;
			mpos += pHeader->size;
			if (pFmt->formatTag != 1) WAV_FAILS("File is not PCM")
			if (!(pFmt->bitsPerSample == 16 || pFmt->bitsPerSample == 8 || pFmt->bitsPerSample == 24
				|| pFmt->bitsPerSample == 32)) WAV_FAILS("File is unsupported bits per sample.")
			snd->channels = pFmt->channels;
			snd->sampleRate = pFmt->sampleRate;
			snd->bitsPerSample = pFmt->bitsPerSample;
		} else if (MATCH_FOURCC(pHeader->id, "data"))
		{
			if (snd->bitsPerSample == 24) {
				snd->data.numBytes = (uint64_t)pHeader->size * 4ll / 3ll;
				snd->data.bytes = (uint8_t *)xm(snd->data.numBytes);
				float *f = (float*)snd->data.bytes;
				wavConvertBuffer b;
				while (pHeader->size > 0) {
					int cnt;
					if (pHeader->size > CBUFFER_BYTES) cnt = CBUFFER_BYTES;
					 else cnt = pHeader->size;
					if (mpos + cnt > in->numBytes) WAV_FAILS("Failed to read data.")
					memcpy(b.c, in->bytes + mpos, cnt);
					mpos += cnt;
					for (int i = 0; i < cnt / 3; i++) {
						*f = ((b.c[i * 3] << 8) + (b.c[i * 3 + 1] << 16) + (b.c[i * 3 + 2] << 24)) / 2147483648.0f;
						f++;
					}
					pHeader->size -= cnt;
				}
			} else if (snd->bitsPerSample == 32) {
				snd->data.numBytes = pHeader->size;
				snd->data.bytes = (uint8_t *)xm(snd->data.numBytes);
				float *f = (float*)snd->data.bytes;
				wavConvertBuffer32 b;
				while (pHeader->size > 0) {
					int cnt;
					if (pHeader->size > CBUFFER_MBYTES) cnt = CBUFFER_MBYTES;
					 else cnt = pHeader->size;
					if (mpos + cnt > in->numBytes) WAV_FAILS("Failed to read data.")
					memcpy(b.c, in->bytes + mpos, cnt);
					mpos += cnt;
					for (int i = 0; i < cnt / 4; i++) {
						*f = (float)b.c[i] / 2147483648.0f;
						f++;
					}
					pHeader->size -= cnt;
				}				
			} else {
				if (mpos + pHeader->size > in->numBytes) WAV_FAILS("File to read data.")
				snd->data.bytes = (unsigned char *)xm(pHeader->size);
				memcpy(snd->data.bytes, in->bytes + mpos, pHeader->size);
				snd->data.numBytes = pHeader->size;
				mpos += pHeader->size;	
			}
		}
	}

serr:
	return err;
}
