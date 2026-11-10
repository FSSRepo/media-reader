# Media Reader

Lightweight C/C++ library for parsing media files. Extract video frames as RGB buffers from MP4 containers and decode audio to raw PCM data from MP3 files.

## Features

- **MP4 Video Parsing**: Extract H.264 (AVC) and H.265 (HEVC) video frames from MP4 containers
- **MP3 Audio Decoding**: Decode MP3 files to raw PCM audio data
- **RGB Conversion**: Convert YUV420 frames to RGB format
- **AAC Audio Support**: Decode AAC audio tracks from MP4 files via FDK-AAC
- **Optional Players**: OpenGL video player and miniaudio audio player (example builds)
- **Single Header Image Output**: Save frames as PNG via stb_image_write

## Supported Formats

| Container | Video Codecs | Audio Codecs |
|-----------|--------------|--------------|
| MP4 | H.264 (AVC1/AVC2/AVC3/AVC4), H.265 (HEVC) | AAC |
| MP3 | — | MPEG-1/2 Audio Layer III |

## Build

Requires **CMake 3.5+** and a C++11 compatible compiler.

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `MR_BUILD_EXAMPLES` | `ON` (standalone) | Build example applications |
| `MR_OGL_PLAYER` | `OFF` | Build with OpenGL video player (requires GLFW3 + OpenGL) |
| `MR_AUDIO_PLAYER` | `OFF` | Build with miniaudio audio player |
| `MR_BUILD_SHARED_LIBS` | `OFF` | Build media-reader as a shared library |

### Example with all options

```bash
cmake .. -DMR_BUILD_EXAMPLES=ON -DMR_OGL_PLAYER=ON -DMR_AUDIO_PLAYER=ON
cmake --build . --config Release
```

## Usage

### Video (MP4)

```cpp
#include "media-reader.h"

// Open MP4 file
mp4_file* mp4 = mp4_open("video.mp4", false);

// Get video track
mp4_track* video = mp4_get_track(mp4, VIDEO_TRACK);

// Read a sample (frame)
mp4_sample sample;
mp4_read_video_sample(mp4, video, 0, sample, true);

// Decode with ffmpeg h264 decoder (see examples)
// Convert YUV420 to RGB
yuv420rgb_multi_buffer(y, u, v, rgb_buffer, width, height);
```

### Audio (MP3)

```cpp
#include "media-reader.h"

// Open and decode entire MP3 file
audio_data* audio = mp3_open("audio.mp3");

// audio->data       - raw PCM data
// audio->samplerate - sample rate (e.g. 44100)
// audio->stereo     - true if stereo
// audio->data_size  - total data size in bytes
```

## External Libraries

All third-party dependencies are included as source in the `external/` folder:

| Library | Source | Purpose |
|---------|--------|---------|
| h264 decoder | [FFmpeg](https://github.com/FFmpeg/FFmpeg) | H.264 video decoding |
| libde265 | [libde265](https://github.com/strukturag/libde265) | H.265/HEVC video decoding |
| stb_image_write | [stb](https://github.com/nothings/stb) | PNG image output |
| FDK AAC | [fdk-aac](https://github.com/mstorsjo/fdk-aac) | AAC audio decoding |
| miniaudio | [miniaudio](https://miniaud.io/) | MP3 decoding and audio playback |

## Limitations

- **GCC 13.2 max** — GCC 14.2 fails with w32threads
- Only **H.264** and **H.265** video codecs are supported
- MP4 audio support is limited to AAC tracks
- Windows-focused build (uses w32threads)
