# sweeps
A dead simple audio resampler re-written from [resweep](https://github.com/SmileTheory/resweep). Handles fixed length
audio in unsigned 8-bit, signed 16-bit, and IEEE 32-bit float formats with 1 or more channels. There is a simple included
wav file reader/writer.

# building
The makefile should detect most desktop OS if built with make on the command line. MSYS2, MINGW, or Cygwin should work
on Windows OS, I have no intention of supporting other compilers (specifically that's MSVC and it's toolchain). There
are no dependencies, except it assumes little endian arch and won't work properly on big endian.

# usage
The makefile builds a standalone executable that can read and resample wav format files in 8 bit, 16 bit, or 
IEEE 32-bit float formats. I have not tested speed on older machines, but on my laptop with an Intel Core i5-12450H
CPU, I get resampling in about ~25x realtime (2 channels, 16 bit). Resampling quality is excellent, and I can't
tell the difference in A/B switch live using Tenacity.

# glitch.wav
This little sample audio is from wikimedia commons: [glitch](https://commons.wikimedia.org/wiki/File:Audionautix-com-ccby-glitch.mp3)
and has the license: *Created by Jason Shaw. Released under Creative Commons Attribution License 3.0. Required credit: "music by audionautix.com". A signed release form is available at http://audionautix.com, CC BY 3.0 <https://creativecommons.org/licenses/by/3.0>, via Wikimedia Commons*