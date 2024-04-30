#include "mp3.h"
#include <string>
#include <string.h>

bool mp3_open(const char* filename) {
    file_stream fs(filename);
    fs.read(3);
    if(std::string(fs.get_string(3)) != "ID3") {
        return false;
    }
    fs.skip(3);
    fs.read(4);
    int tag_size = (fs.get() << (7*3)) | (fs.get() << (7*2)) | (fs.get() << 7) | fs.get();
    fs.skip(tag_size);

    // begin frames
    int num_frames = 0;
    while(!fs.end_of_file(4)) {
        fs.read(4);
        uint32_t frame_info = fs.next_uint32();
        if((frame_info & 0xfff00000) != 0xfff00000) {
            break;
        }
        uint32_t id = (frame_info & 0x00080000) >> 19;
        int layer = (frame_info & 0x00060000) >> 17;

        uint32_t protection_bit = (frame_info & 0x00010000) >> 16;

        int32_t m_bitrate_index = (frame_info & 0x0000f000) >> 12;

        int32_t m_sr_index = (frame_info & 0x00000c00) >> 10;

        int32_t padding_bit = (frame_info & 0x00000200) >> 9;

        uint32_t m_mode = (frame_info & 0x000000c0) >> 6;

        uint8_t m_mode_extension = (frame_info & 0x00000030) >> 4;

        if(!protection_bit) {
            fs.skip(2);
        }

        uint32_t frame_data_size = ((144 * bitrates_layer3_list[m_bitrate_index]) / samplerates_layer3_list[m_sr_index] + padding_bit) - (protection_bit ? 0 : 2);
        frame_data_size -= 4; // exclude header word

        // int side_info = m_mode == 3 /* mono */ ? 17 : 32;

        // fs.skip(side_info);
        // fs.read(4);
        fs.skip(frame_data_size);
        
        printf("frame: %d - %d - %d\n", num_frames, frame_data_size, fs.file_size);
        num_frames++;
    }
    printf("num frames: %d - 0x%X", num_frames, fs.count);
    return true;
}