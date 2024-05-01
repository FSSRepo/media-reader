#ifndef __MP3_H__
#define __MP3_H__

#include <vector>
#include "filestream.h"
#include "mp3/tables.h"

enum mp3_mode {
    MODE_MONO,
    MODE_STEREO
};

struct mp3_frame {
    int bitrate;
    int samplerate;
    mp3_mode mode;
    int size = 0;
    uint8_t* data;
};

bool mp3_open(const char* filename);

#endif