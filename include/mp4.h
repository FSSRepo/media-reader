#ifndef __MP4_H__
#define __MP4_H__

#include <vector>
#include "filestream.h"

struct mp4_chunk_group {
    int first_chunk;
    int chunk_count;
    int first_sample;
    int samples_per_chunk;
    int samples_desc_index; // unused
};

enum mp4_bitstream_type {
    H264_AVC1,
    H264_AVC2,
    H264_AVC3,
    H264_AVC4,
    H265_HEVC
};

#define HEVC_NALU_VPS 32
#define HEVC_NALU_SPS 33
#define HEVC_NALU_PPS 34

struct nal_chunk {
    uint8_t* data;
    int size;
};

struct mp4_sequence {
    uint8_t nalu_type;
    std::vector<nal_chunk> nalus;
};

struct mp4_sample {
    uint32_t offset;
    uint32_t size = 0;
    uint8_t* data = NULL;
};

enum mp4_track_type {
    VIDEO_TRACK,
    AUDIO_TRACK
};

struct mp4_track {
    std::vector<uint32_t>        samples_sizes;
    std::vector<mp4_chunk_group> samples_groups;
    std::vector<uint32_t>        chunk_offsets;
    mp4_track_type type;
    mp4_bitstream_type bs_type;

    // NAL payload
    std::vector<mp4_sequence> sequences_hevc;
    std::vector<nal_chunk> sequences_nal;
    std::vector<nal_chunk> pictures_nal;
    int nalu_length_size;
    uint8_t begin_payload_packet;

    // sampling params
    int sample_count;
    int time_scale;
    int duration;
    int sample_rate;
    int16_t num_channels;

    // video params
    int width;
    int height;
    float fps;
};

struct mp4_file {
    std::vector<struct mp4_track> tracks;
    struct file_stream* fs = NULL;
    uint8_t nal_head[4] = { 0, 0, 0, 1 };
    uint8_t nal_body[3] = { 0, 0, 1 };
};

struct mp4_file* mp4_open(const char* filename, bool verbose);
void mp4_get_video_sample(struct mp4_track* track, int index, mp4_sample &sample);
void mp4_nearest_iframe(struct mp4_file* mp4, struct mp4_track* track, int index, int* result);
void mp4_read_video_sample(struct mp4_file* mp4, struct mp4_track* track, int index, mp4_sample &sample, bool include_nal_unit);
struct mp4_track* mp4_get_track(struct mp4_file* mp4, mp4_track_type type);

void mp4_free_sample(mp4_sample &sample);

#endif // __MP4_H__