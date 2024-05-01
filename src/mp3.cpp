#include "mp3.h"
#include "mp3/decode.h"
#include <string>
#include <string.h>

bool mp3_open(const char* filename) {
    file_stream fs(filename);
    // begin frames
    int num_frames = 0;
    bool info_skipped = false;
    mp3_frame frame;
    frame.data = NULL;
    int16_t* buffer = (int16_t*)malloc(2 * 1152 * sizeof(int16_t));

    while(!fs.end_of_file(4)) {
        if(num_frames == 3) {
            break;
        }
        fs.read(4);
        uint32_t frame_info = fs.next_uint32();
        if((frame_info & 0xfff00000) != 0xfff00000) {
            // skip ID3 tags - some mp3 files are concatened
            fs.set(fs.count - 4);
            fs.read(3);
            if(std::string(fs.get_string(3)) == "ID3") {
                fs.skip(3);
                fs.read(4);
                int tag_size = (fs.get() << (7*3)) | (fs.get() << (7*2)) | (fs.get() << 7) | fs.get();
                fs.skip(tag_size);
                continue;
            } else {
                return false;
            }
            break;
        }

        int8_t id =                 (frame_info & 0x00080000) >> 19;
        int8_t layer =              (frame_info & 0x00060000) >> 17;
        int8_t protection_bit =     (frame_info & 0x00010000) >> 16;
        int8_t m_bitrate_index =    (frame_info & 0x0000f000) >> 12;
        int8_t m_sr_index =         (frame_info & 0x00000c00) >> 10;
        int8_t padding_bit =        (frame_info & 0x00000200) >> 9;
        int8_t m_mode =             (frame_info & 0x000000c0) >> 6;
        int8_t m_mode_extension =   (frame_info & 0x00000030) >> 4;

        if(!protection_bit) {
            fs.skip(2);
        }
        
        frame.bitrate = bitrates_layer3_list[m_bitrate_index];
        frame.samplerate = samplerates_layer3_list[m_sr_index];
        frame.mode = m_mode == 3 ? MODE_MONO : MODE_STEREO;

        uint32_t new_size = ((144 * frame.bitrate) / frame.samplerate + padding_bit) - (protection_bit ? 0 : 2);
        if(frame.size < new_size) {
            if(frame.data) {
                free(frame.data);
                frame.data = NULL;
            }
            frame.data = (uint8_t*)malloc(new_size);
            frame.size = new_size;
            printf("new buffer\n");
        }
        frame.size -= 4; // exclude header word

        // skip info frame
        if(!info_skipped) {
            int side_info = frame.mode == MODE_MONO ? 17 : 32;
            int cur_ = fs.count;
            fs.skip(side_info);
            fs.read(4);
            if(std::string(fs.get_string(4)) == "Info") {
                fs.set(cur_ + frame.size);
                info_skipped = true;
                continue;
            } else {
                fs.set(cur_);
            }
        }

        // read frame
        fs.read(frame.data, frame.size);
        // decode phase
        mp3_decode(frame, buffer);
        printf("frame: %d - %d - %d\n", num_frames,  frame.size, fs.file_size);
        num_frames++;
    }
    int time = (num_frames*1152) / frame.samplerate;
    printf("num frames: %d - %d:%d", num_frames, time/60, time % 60);
    return true;
}