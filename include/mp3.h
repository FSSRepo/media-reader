#ifndef __MP3_H__
#define __MP3_H__

#include <vector>
#include "filestream.h"

enum mp3_mode {
    MODE_MONO,
    MODE_STEREO
};

struct mp3_frame {
    int bitrate;
    int samplerate;
    mp3_mode mode;
    int mode_extension;
    int size = 0;
    uint8_t* data;
    uint8_t* stream_data;
    uint8_t m_sr_index;

    float m_hs_store[2][32][18];
	float m_sbs_v_vec[2][1024];
};

struct audio_data {
    void* data;
    int samplerate;
    bool stereo;
    size_t data_size;
};

audio_data* mp3_open(const char* filename);

#endif