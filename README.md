# sweeps
A dead simple audio resampler re-written from [resweep](https://github.com/SmileTheory/resweep). Handles fixed length audio in unsigned 8-bit, signed 16-bit, and IEEE 32-bit float formats with 1 or more channels. There is a simple included wav file reader/writer.

# building
The makefile should detect most desktop OS if built with make on the command line. MSYS2, MINGW, or Cygwin should work on Windows OS, I have no intention of supporting other compilers (specifically that's MSVC and it's toolchain). There are no dependencies, except it assumes little endian arch and won't work properly on big endian.

# usage
The makefile builds a standalone executable that can read and resample wav format files in 8 bit, 16 bit, or IEEE 32-bit float formats. A small set of tests: my Windows 11 laptop with an Intel Core i5-12450H CPU and my iMac intel i5-4590s, I get resampling in about:
 - 12th gen: ~25x realtime - 2 channels, 16 bit, 44100 to 48000
 - 12th gen: ~13x realtime - 2 channels, 16 bit, 44100 to 96000
 - 12th gen: ~14x realtime - 2 channels, 16 bit, 44100 to 22050
 - 4th gen: ~12x realtime - 2 channels, 16 bit, 44100 to 48000
 - 4th gen: ~6x realtime - 2 channels, 16 bit, 44100 to 96000
 - 4th gen: ~6x realtime - 2 channels, 16 bit, 44100 to 22050

So it seems efficiency will very quite a bit depending on the conversion needed. Resampling quality is excellent, and I can't tell the difference in A/B switch live using Tenacity. Performance on 4th gen Intel seems perfectly fine too, though obviously slower than modern hardware.

The mwav.h header supports PHYSFS, if you define WAV_USE_PHYSFS before including the header.

You can remove the wav stuff entirely by defining SWEEPS_NO_MWAV before including the header.

# todo
More testing, I wrote in float to float resampling but it is untested. Also I want to support resampling of streams, passing in buffer chunks, so that's on the list. That is about it.

# glitchXX.wav
This little sample audio is from wikimedia commons: [glitch](https://commons.wikimedia.org/wiki/File:Audionautix-com-ccby-glitch.mp3)
and has the license: *Created by Jason Shaw. Released under Creative Commons Attribution License 3.0. Required credit: "music by audionautix.com". A signed release form is available at http://audionautix.com, CC BY 3.0 <https://creativecommons.org/licenses/by/3.0>, via Wikimedia Commons*

The glitch16.wav is 16-bit and the glitch24.wav is 24-bit PCM audio versions (used for testing 24 bit read/write and conversion to and from float).