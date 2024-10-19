#include "mp3.h"
#include "mp3/decode.h"
#include "tables.h"
#include <string>
#include <string.h>
#include "types.h"

audio_data* mp3_open(const char* filename) {
    file_stream fs(filename);
    // begin frames
    int num_frames = 0;
    bool info_skipped = false;
    mp3_frame frame;
    frame.data = NULL;
    float buffer[2][1152];

    audio_data* audio = new audio_data{NULL, 0, false};

    // decoder memory
    uint8_t* rbuffer = (uint8_t*)malloc(sizeof(OpenMP3::Reservoir));
	memset(rbuffer, 0, sizeof(OpenMP3::Reservoir));
	OpenMP3::Reservoir & br = *(OpenMP3::Reservoir*)(rbuffer);

    memset(frame.m_hs_store, 0, sizeof(frame.m_hs_store));
	memset(frame.m_sbs_v_vec, 0, sizeof(frame.m_sbs_v_vec));

    std::vector<float> audio_data[2];

    while(!fs.end_of_file(4)) {
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
                return NULL;
            }
            break;
        }

        int8_t id =                 (frame_info & 0x00080000) >> 19;
        int8_t layer =              (frame_info & 0x00060000) >> 17;
        int8_t protection_bit =     (frame_info & 0x00010000) >> 16;
        int8_t m_bitrate_index =    (frame_info & 0x0000f000) >> 12;
        frame.m_sr_index =          (frame_info & 0x00000c00) >> 10;
        int8_t padding_bit =        (frame_info & 0x00000200) >> 9;
        int8_t m_mode =             (frame_info & 0x000000c0) >> 6;
        frame.mode_extension =      (frame_info & 0x00000030) >> 4;

        if(!protection_bit) {
            fs.skip(2);
        }
        
        frame.bitrate = bitrates_layer3_list[m_bitrate_index];
        frame.samplerate = samplerates_layer3_list[frame.m_sr_index];
        frame.mode = m_mode == 3 ? MODE_MONO : MODE_STEREO;

        uint32_t new_size = ((144 * frame.bitrate) / frame.samplerate + padding_bit) - (protection_bit ? 0 : 2);
        if(frame.size < new_size) {
            if(frame.data) {
                free(frame.data);
                frame.data = NULL;
            }
            frame.data = (uint8_t*)malloc(new_size);
            frame.size = new_size;
        }
        frame.size -= 4; // exclude header word

        // read frame
        fs.read(frame.data, frame.size);
        
        // decode phase
        if(mp3_decode(frame, buffer, br)) {
            for(int ch = 0; ch < 2; ++ch) {
                auto & channel = audio_data[ch];
                auto * data = buffer[ch];
                for (OpenMP3::UInt idx = 0; idx < 1152; ++idx) channel.push_back(*data++);
		    }
        }

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
        num_frames++;
    }
    int time = (num_frames*1152) / frame.samplerate;

    printf("duration: %d minutes, %d seconds\n", time/60, time % 60);
    
    int length = audio_data[0].size();
    {
	// FILE* wav = fopen("out.wav", "wb");
	// uint8_t* wav_header = new uint8_t[44];
	// memcpy(wav_header, "RIFF\xff\xff\xff\xffWAVEfmt ", 16);
	// uint16_t channels_ = (frame.mode == MODE_MONO ? 1 : 2);
	// int file_size_ = 44 + channels_ * 2 * length;
	// memcpy(wav_header + 4, &file_size_, 4);
	// int tmp = 16; // size
	// memcpy(wav_header + 16, &tmp, 4);
	// short tmp2 = 1; // PCM
	// memcpy(wav_header + 20, &tmp2, 2);
	// memcpy(wav_header + 22, &channels_, 2);
	// tmp = frame.samplerate; // sample rate
	// memcpy(wav_header + 24, &tmp, 4);
	// tmp = frame.samplerate * channels_ * 2; // byte rate
	// memcpy(wav_header + 28, &tmp, 4);
	// tmp2 = 4; // align
	// memcpy(wav_header + 32, &tmp2, 2);
	// tmp2 = 16; // bits per sample
	// memcpy(wav_header + 34, &tmp2, 2);
	// memcpy(wav_header + 36, "data\xff\xff\xff\xff", 8);
	// int data_size = channels_ * 2 * length;
	// memcpy(wav_header + 40, &data_size, 4);
	// fwrite(wav_header, 1, 44, wav);
	// for(int j = 0; j < length; j++) {
	// 	for(int i = 0;i < channels_;i ++) {
	// 		int x = (int)(audio_data[i][j] * 32767.f);
	// 		// clamp
	// 		x = x > 32767 ? 32767 : (x < -32767 ? -32767 : x);
	// 		int16_t x_a = x;
	// 		fwrite(&x_a, 1, 2, wav);
	// 	}
	// }
	// fclose(wav);
    }
    int index = 0;
    
    audio->stereo = frame.mode != MODE_MONO;

    audio->data_size = length * (audio->stereo ? 2 : 1) * sizeof(int16_t);

    audio->data = malloc(audio->data_size);
    

    for(int j = 0; j < length; j++) {
		for(int i = 0;i < (audio->stereo ? 2 : 1); i ++) {
			int x = (int)(audio_data[i][j] * 32767.f);
			// clamp
			x = x > 32767 ? 32767 : (x < -32767 ? -32767 : x);
			((int16_t*)audio->data)[index] = x;
            index++;
		}
	}
    audio_data[0].clear();
    audio_data[1].clear();
    audio->samplerate = frame.samplerate;
    return audio;
}