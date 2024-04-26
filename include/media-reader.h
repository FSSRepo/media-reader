#include <vector>

struct sample_group {
    int first_chunk;
    int chunk_count;
    int first_sample;
    int samples_per_chunk;
    int samples_desc_index; // unused
};

struct nal_chunk {
    uint8_t* data;
    int size;
};


struct data_sample {
    uint32_t offset;
    uint32_t size = 0;
    uint8_t* data = NULL;
};

struct MP4 {
    int width, height;
    float fps;
    int sample_count;
    int time_scale;
    int duration;

    bool moov = false;
    std::vector<uint32_t> samples_sizes;
    std::vector<sample_group> samples_groups;
    std::vector<uint32_t> chunk_offsets;
    bool video_track = false;
    bool media_type_video = false;
    std::vector<nal_chunk> sequences_nal;
    std::vector<nal_chunk> pictures_nal;
    int nalu_length_size;
    uint8_t begin_payload_packet;

    FILE* file;
    uint8_t* tmp_buffer = new uint8_t[4];
    uint8_t nal_head[4] = { 0, 0, 0, 1 };
    uint8_t nal_body[3] = { 0, 0, 1 };
};

struct MP4* read_mp4(const char* filename, bool verbose);
void find_nearest_key_frame(struct MP4* mp4, int index, int* result);
void read_sample(struct MP4* mp4, int index, data_sample &sample, bool include_nal_unit);
void yuv420rgb(uint8_t* yuv, uint8_t* rgb, int width, int height);