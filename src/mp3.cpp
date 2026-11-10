#include "mp3.h"
#include "miniaudio.h"
#include <cstdlib>
#include <cstring>
#include <vector>

audio_data* mp3_open(const char* filename) {
    if (!filename) return NULL;
    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);
    if (ma_decoder_init_file(filename, &config, &decoder) != MA_SUCCESS) return NULL;
    if (decoder.outputChannels < 1 || decoder.outputChannels > 2 || decoder.outputSampleRate == 0) {
        ma_decoder_uninit(&decoder);
        return NULL;
    }
    std::vector<ma_int16> samples;
    ma_int16 buffer[4096 * 2];
    ma_uint64 frames = 0;
    ma_result result;
    do {
        result = ma_decoder_read_pcm_frames(&decoder, buffer, 4096, &frames);
        samples.insert(samples.end(), buffer, buffer + frames * decoder.outputChannels);
    } while (result == MA_SUCCESS && frames > 0);
    if ((result != MA_SUCCESS && result != MA_AT_END) || samples.empty()) {
        ma_decoder_uninit(&decoder);
        return NULL;
    }
    audio_data* audio = new audio_data;
    audio->data_size = samples.size() * sizeof(ma_int16);
    audio->data = malloc(audio->data_size);
    if (!audio->data) {
        delete audio;
        ma_decoder_uninit(&decoder);
        return NULL;
    }
    memcpy(audio->data, samples.data(), audio->data_size);
    audio->samplerate = decoder.outputSampleRate;
    audio->stereo = decoder.outputChannels == 2;
    ma_decoder_uninit(&decoder);
    return audio;
}
