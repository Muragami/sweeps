/*
	mwav.h

	Very basic .wav file load and save, supports:
		* stdio
		* memory blocks
		* PHYSFS (if WAV_USE_PHYSFS is defined before inclusion)
	
	only accepts/handles:
		8 or 16 bit PCM and 32-bit IEEE float uncompressed files

	muragami, muragami@wishray.com, Jason A. Petrasko 2024
	MIT license: https://opensource.org/license/mit/
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef WAV_USE_PHYSFS
#include <physfs.h>
#endif

#define WRITE_FOURCC(dst, src) 	((dst)[0] = (src)[0], (dst)[1] = (src)[1],\
								(dst)[2] = (src)[2], (dst)[3] = (src)[3])
#define MATCH_FOURCC(dst, src) 	((dst)[0] == (src)[0] && (dst)[1] == (src)[1]\
								 && (dst)[2] == (src)[2] && (dst)[3] == (src)[3])
#define WAV_FAIL(x)				{ err = x; goto ferr; }
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

typedef void* (*xmalloc)(size_t x);

// ******************************************************************************
// File load and save

static const char* __attribute__((unused)) wavSaveFile(const char *filename, wavSound *snd) {
	FILE *fp;
	wavSaveHeader head;
	const char *err = NULL;

	WRITE_FOURCC(head.riffHeader.id, "RIFF");
	head.riffHeader.size = snd->data.numBytes + sizeof(head) - sizeof(head.riffHeader);
	WRITE_FOURCC(head.id_WAVE, "WAVE");

	WRITE_FOURCC(head.fmt_Header.id, "fmt ");
	head.fmt_Header.size = 16;
	head.fmt_Data.formatTag = 1; // Microsoft PCM format
	head.fmt_Data.channels = snd->channels;
	head.fmt_Data.sampleRate = snd->sampleRate;
	head.fmt_Data.avgBytesPerSec = snd->sampleRate * snd->channels * snd->bitsPerSample / 8;
	head.fmt_Data.blockAlign = snd->bitsPerSample / 2;
	head.fmt_Data.bitsPerSample = snd->bitsPerSample;

	WRITE_FOURCC(head.dataHeader.id, "data");
	head.dataHeader.size = snd->data.numBytes;

	if (!(fp = fopen(filename, "wb"))) WAV_FAIL("Failed to open file")
	if (fwrite((unsigned char *)&head, 1, sizeof(head), fp) != sizeof(head)) WAV_FAIL("Failed to write header")
	if (fwrite(snd->data.bytes, 1, snd->data.numBytes, fp) != snd->data.numBytes) WAV_FAIL("Failed to write data")
	
	fclose(fp);
	return err;

ferr:
	if (fp != NULL) fclose(fp);
	return err;
}

static const char* __attribute__((unused)) wavLoadFile(const char *filename, wavSound *snd, xmalloc xm) {
	wavChunkHeader chunkHeader;
	char waveId[4];
	FILE *fp = NULL;
	const char *err = NULL;

	if (xm == NULL) xm = malloc;

	if (!(fp = fopen(filename, "rb"))) WAV_FAIL("Failed to open file")
	if (fread(&chunkHeader, 1, sizeof(chunkHeader), fp) != sizeof(chunkHeader)) WAV_FAIL("Failed to read RIFF header")
	if (!MATCH_FOURCC(chunkHeader.id, "RIFF")) WAV_FAIL("File is not RIFF")
	if (fread(waveId, 1, 4, fp) != 4) WAV_FAIL("Failed to read WAVE header")
	if (!MATCH_FOURCC(waveId, "WAVE")) WAV_FAIL("File is not RIFF WAVE")

	while (1)
	{
		size_t endPos;
		if (fread(&chunkHeader, 1, sizeof(chunkHeader), fp) != sizeof(chunkHeader)) break;

		endPos = ftell(fp) + chunkHeader.size;

		if (MATCH_FOURCC(chunkHeader.id, "fmt ")) {
			wavFmtData fmtData;

			if (chunkHeader.size < sizeof(fmtData)) WAV_FAIL("Badly formatted 'fmt ' chunk")
			if (fread(&fmtData, 1, sizeof(fmtData), fp) != sizeof(fmtData)) WAV_FAIL("Failed to read 'fmt_' chunk")
			if (!(fmtData.formatTag == 1 || fmtData.formatTag == 3)) WAV_FAIL("File is not PCM")
			if (fmtData.formatTag == 3 && fmtData.bitsPerSample != 32) WAV_FAIL("File is not 32-bit floating point.")
			if (fmtData.formatTag == 1 && fmtData.bitsPerSample > 16) WAV_FAIL("File is not 8 or 16-bit PCM.")
			snd->channels = fmtData.channels;
			snd->sampleRate = fmtData.sampleRate;
			snd->bitsPerSample = fmtData.bitsPerSample;
		} else if (MATCH_FOURCC(chunkHeader.id, "data"))
		{
			snd->data.bytes = (unsigned char *)xm(chunkHeader.size);
			chunkHeader.size = fread(snd->data.bytes, 1, chunkHeader.size, fp);
			snd->data.numBytes = chunkHeader.size;
		}

		fseek(fp, endPos, SEEK_SET);
	}

	fclose(fp);

	return err;

ferr:
	if (fp != NULL) fclose(fp);
	return err;
}

// ******************************************************************************
// Memory load and save

static const char* __attribute__((unused)) wavSaveMemory(const char *filename, wavSound *snd, wavData *out, xmalloc xm) {
	wavSaveHeader head;

	if (xm == NULL) xm = malloc;
	out->bytes = (uint8_t*)xm(sizeof(head) + snd->data.numBytes);
	if (out->bytes == NULL) return "xmalloc() allocation failed";
	out->numBytes = sizeof(head) + snd->data.numBytes;

	WRITE_FOURCC(head.riffHeader.id, "RIFF");
	head.riffHeader.size = snd->data.numBytes + sizeof(head) - sizeof(head.riffHeader);
	WRITE_FOURCC(head.id_WAVE, "WAVE");

	WRITE_FOURCC(head.fmt_Header.id, "fmt ");
	head.fmt_Header.size = 16;
	head.fmt_Data.formatTag = 1; // Microsoft PCM format
	head.fmt_Data.channels = snd->channels;
	head.fmt_Data.sampleRate = snd->sampleRate;
	head.fmt_Data.avgBytesPerSec = snd->sampleRate * snd->channels * snd->bitsPerSample / 8;
	head.fmt_Data.blockAlign = snd->bitsPerSample / 2;
	head.fmt_Data.bitsPerSample = snd->bitsPerSample;

	WRITE_FOURCC(head.dataHeader.id, "data");
	head.dataHeader.size = snd->data.numBytes;

	memcpy(out->bytes, &head, sizeof(head));
	memcpy(out->bytes + sizeof(head), snd->data.bytes, snd->data.numBytes);

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
			mpos += sizeof(wavFmtData);
			if (!(pFmt->formatTag == 1 || pFmt->formatTag == 3)) WAV_FAILS("File is not PCM")
			if (pFmt->formatTag == 3 && pFmt->bitsPerSample != 32) WAV_FAILS("File is not 32-bit floating point.")
			if (pFmt->formatTag == 1 && pFmt->bitsPerSample > 16) WAV_FAILS("File is not 8 or 16-bit PCM.")
			snd->channels = pFmt->channels;
			snd->sampleRate = pFmt->sampleRate;
			snd->bitsPerSample = pFmt->bitsPerSample;
		} else if (MATCH_FOURCC(pHeader->id, "data"))
		{
			if (mpos + pHeader->size > in->numBytes) WAV_FAILS("File data is not properly sized")
			snd->data.bytes = (unsigned char *)xm(pHeader->size);
			memcpy(snd->data.bytes, in->bytes + mpos, pHeader->size);
			snd->data.numBytes = pHeader->size;
			mpos += pHeader->size;
		}
	}

serr:
	return err;
}

// ******************************************************************************
// PHYSFS load and save

#ifdef WAV_USE_PHYSFS

static const char* __attribute__((unused)) wavSavePFile(const char *filename, wavSound *snd) {
	PHYSFS_File *pfp;
	wavSaveHeader head;
	const char *err = NULL;

	WRITE_FOURCC(head.riffHeader.id, "RIFF");
	head.riffHeader.size = snd->data.numBytes + sizeof(head) - sizeof(head.riffHeader);
	WRITE_FOURCC(head.id_WAVE, "WAVE");

	WRITE_FOURCC(head.fmt_Header.id, "fmt ");
	head.fmt_Header.size = 16;
	head.fmt_Data.formatTag = 1; // Microsoft PCM format
	head.fmt_Data.channels = snd->channels;
	head.fmt_Data.sampleRate = snd->sampleRate;
	head.fmt_Data.avgBytesPerSec = snd->sampleRate * snd->channels * snd->bitsPerSample / 8;
	head.fmt_Data.blockAlign = snd->bitsPerSample / 2;
	head.fmt_Data.bitsPerSample = snd->bitsPerSample;

	WRITE_FOURCC(head.dataHeader.id, "data");
	head.dataHeader.size = snd->data.numBytes;

	if (!(pfp = PHYSFS_openWrite(filename))) WAV_FAIL("Failed to open file")
	if (PHYSFS_writeBytes(pfp, &head, sizeof(head)) != sizeof(head)) WAV_FAIL("Failed to write header")
	if (PHYSFS_writeBytes(pfp, snd->data.bytes, snd->data.numBytes) != snd->data.numBytes) WAV_FAIL("Failed to write data")
	
	PHYSFS_close(pfp);
	return err;

ferr:
	if (pfp != NULL) PHYSFS_close(pfp);
	return err;
}

static const char* __attribute__((unused)) wavLoadPFile(const char *filename, wavSound *snd, xmalloc xm) {
	wavChunkHeader chunkHeader;
	char waveId[4];
	PHYSFS_File *pfp = NULL;
	const char *err = NULL;

	if (xm == NULL) xm = malloc;

	if (!(pfp = PHYSFS_openRead(filename))) WAV_FAIL("Failed to open file")
	if (PHYSFS_readBytes(pfp, &chunkHeader, sizeof(chunkHeader)) != sizeof(chunkHeader)) WAV_FAIL("Failed to read RIFF header")
	if (!MATCH_FOURCC(chunkHeader.id, "RIFF")) WAV_FAIL("File is not RIFF")
	if (PHYSFS_readBytes(pfp, waveId, 4) != 4) WAV_FAIL("Failed to read WAVE header")
	if (!MATCH_FOURCC(waveId, "WAVE")) WAV_FAIL("File is not RIFF WAVE")

	while (1)
	{
		size_t endPos;
		if (PHYSFS_readBytes(pfp, &chunkHeader, sizeof(chunkHeader)) != sizeof(chunkHeader)) break;

		endPos = PHYSFS_tell(pfp) + chunkHeader.size;

		if (MATCH_FOURCC(chunkHeader.id, "fmt ")) {
			wavFmtData fmtData;

			if (chunkHeader.size < sizeof(fmtData)) WAV_FAIL("Badly formatted 'fmt ' chunk")
			if (PHYSFS_readBytes(pfp, &fmtData, sizeof(fmtData)) != sizeof(fmtData)) WAV_FAIL("Failed to read 'fmt_' chunk")
			if (!(fmtData.formatTag == 1 || fmtData.formatTag == 3)) WAV_FAIL("File is not PCM")
			if (fmtData.formatTag == 3 && fmtData.bitsPerSample != 32) WAV_FAIL("File is not 32-bit floating point.")
			if (fmtData.formatTag == 1 && fmtData.bitsPerSample > 16) WAV_FAIL("File is not 8 or 16-bit PCM.")
			snd->channels = fmtData.channels;
			snd->sampleRate = fmtData.sampleRate;
			snd->bitsPerSample = fmtData.bitsPerSample;
		} else if (MATCH_FOURCC(chunkHeader.id, "data"))
		{
			snd->data.bytes = (unsigned char *)xm(chunkHeader.size);
			chunkHeader.size = PHYSFS_readBytes(pfp, snd->data.bytes, chunkHeader.size);
			snd->data.numBytes = chunkHeader.size;
		}

		PHYSFS_seek(pfp, endPos);
	}

	PHYSFS_close(pfp);

	return err;

ferr:
	if (pfp != NULL) PHYSFS_close(pfp);
	return err;
}

#endif