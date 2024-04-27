#include <string>
#include <string.h>
#include "mp4.h"

int parse_mp4_box(file_stream* fs, int offset, int level, mp4_file* mp4, bool read_track, bool verbose) {
    fs->flush(8);
    int size = fs->next_uint32();
    std::string type = std::string(fs->get_string(4));
    if(verbose) {
        for(int i = 0; i < level; i++) {
            printf("    ");
        }
        printf("%s [%d bytes]\n", type.c_str(), size);
    }
    if(type == "tkhd") {
        fs->read(4);
        uint8_t version = fs->get();
        fs->skip(52 + (version ? 32 : 20)); // skip version + flags + dates
        fs->read(8);
        struct mp4_track track;
        track.width = fs->next_uint32() / 65536;
        track.height = fs->next_uint32() / 65536;
        mp4->tracks.push_back(track);
    } else if(read_track) {
        struct mp4_track* track = &mp4->tracks[mp4->tracks.size() - 1];
        if(type == "mdhd") {
            fs->read(4);
            uint8_t version = fs->get();
            if(version) {
                fs->skip(16);
                fs->read(12);
                track->time_scale = fs->next_uint32();
                track->duration = fs->next_uint64();
            } else {
                fs->skip(8);
                fs->read(8);
                track->time_scale = fs->next_uint32();
                track->duration = fs->next_uint32();
            }
        } else if(type == "hdlr") {
            fs->skip(8);
            fs->read(4);
            // ignore other unhandled track types
            std::string media_type = fs->get_string(4);
            if(media_type == "vide") {
                track->type = VIDEO_TRACK;
            } else if(media_type == "soun") {
                track->type = AUDIO_TRACK;
            } else {
                return -2; // skip track
            }
        } else if(type == "stsz") {
            fs->skip(4); // skip version + flags
            fs->read(8);
            uint32_t sample_size = fs->next_uint32();
            track->sample_count = fs->next_uint32();
            track->fps = (float)(track->sample_count / (track->duration * 1.0f / track->time_scale));
            if(sample_size == 0) {
                if((size - 12 - 8) != track->sample_count * 4) {
                    return -1;
                }
                for(int i = 0; i < track->sample_count; i++) {
                    fs->read(4);
                    track->samples_sizes.push_back(fs->next_uint32());
                }
            }
        } else if(type == "stsc") {
            fs->skip(4); // skip version + flags
            fs->read(4);
            uint32_t entry_count = fs->next_uint32();
            if((size - 12 - 4) == entry_count*12) {
                uint32_t first_sample = 1;
                for(uint32_t i = 0; i < entry_count; i++) {
                    fs->read(12);
                    struct mp4_chunk_group group;
                    group.first_chunk           = fs->next_uint32();
                    group.samples_per_chunk     = fs->next_uint32();
                    group.samples_desc_index    = fs->next_uint32();
                    if(i) {
                        struct mp4_chunk_group* prev = &track->samples_groups[i - 1];
                        prev->chunk_count = group.first_chunk - prev->first_chunk;
                        first_sample += prev->chunk_count * prev->samples_per_chunk;
                        if(track->type == VIDEO_TRACK) {
                             printf("point = %d - %d\n",   group.first_chunk, group.samples_per_chunk);
                        }
                    }
                    group.chunk_count = 0;
                    group.first_sample = first_sample;
                    track->samples_groups.push_back(group);
                }
            }
        } else if(type == "stco") {
            fs->skip(4); // skip version + flags
            fs->read(4);
            uint32_t entry_count = fs->next_uint32();
            if((size - 12 - 4) == entry_count*4) {
                for(uint32_t i = 0; i < entry_count; i++) {
                    fs->read(4);
                    track->chunk_offsets.push_back(fs->next_uint32());
                }
            }
        } else if(type == "stsd") {
            fs->skip(4); // skip version + flags
            fs->read(4);
            int entry_count = fs->next_uint32();
            if(entry_count != 1) {
                fprintf(stderr, "MP4 error");
                return -1;
            }
        } else if(type == "avc1" || type == "avc2" || type == "avc3" || type == "avc4" || type == "hvc1") { // sample descriptor
            fs->skip(24);
            fs->read(4);
            uint16_t width = fs->next_uint16();
            uint16_t height = fs->next_uint16();
            if(track->width != width || track->height != height) {
                fprintf(stderr, "MP4 error");
                return -1;
            }
            fs->skip(50);
            track->bs_type = (
                    type == "avc2" ? H264_AVC2 : (
                        type == "avc3" ? H264_AVC3 :  (
                            type == "avc4" ? H264_AVC4 : (
                                type == "hvc1" ? H265_HEVC : H264_AVC1))));
        } else if(type == "avcC") { // sample descriptor
            int payload_size = size - 8;
            uint8_t* payload = (uint8_t*)malloc(payload_size);
            fs->read(payload, payload_size);
            track->nalu_length_size = 1 + (payload[4] & 3);
            uint8_t num_seq_params = payload[5] & 31;
            uint32_t cursor = 6;
            for (uint32_t i = 0; i < num_seq_params; i++) {
                if (cursor+2 <= payload_size) {
                    uint16_t param_length = read_uint16_be(&payload[cursor]);
                    cursor += 2;
                    if (cursor + param_length <= payload_size) {
                        nal_chunk chunk;
                        chunk.data = new uint8_t[param_length];
                        chunk.size = param_length;
                        memcpy(chunk.data, &payload[cursor], param_length);
                        track->sequences_nal.push_back(chunk);
                        cursor += param_length;
                    }
                }
            }
            if (cursor >= payload_size) {
                return -1;
            }
            uint8_t num_pic_params = payload[cursor++];
            for (uint32_t i = 0; i < num_pic_params; i++) {
                if (cursor+2 <= payload_size) {
                    uint8_t param_length = read_uint16_be(&payload[cursor]);
                    cursor += 2;
                    if (cursor + param_length <= payload_size) {
                        nal_chunk chunk;
                        chunk.data = new uint8_t[param_length];
                        chunk.size = param_length;
                        memcpy(chunk.data, &payload[cursor], param_length);
                        track->pictures_nal.push_back(chunk);
                        cursor += param_length;
                    }
                }
            }
            free(payload);
        } else if(type == "hvcC") { // sample descriptor h265
            int payload_size = size - 8;
            uint8_t* payload = (uint8_t*)malloc(payload_size);
            fs->read(payload, payload_size);
            // m_ConfigurationVersion   = payload[0];
            // m_GeneralProfileSpace    = (payload[1]>>6) & 0x03;
            // m_GeneralTierFlag        = (payload[1]>>5) & 0x01;
            // m_GeneralProfile         = (payload[1]   ) & 0x1F;
            // m_GeneralProfileCompatibilityFlags = AP4_BytesToUInt32BE(&payload[2]);
            // m_GeneralConstraintIndicatorFlags  = (((AP4_UI64)AP4_BytesToUInt32BE(&payload[6]))<<16) | AP4_BytesToUInt16BE(&payload[10]);
            // m_GeneralLevel           = payload[12];
            // m_Reserved1              = (payload[13]>>4) & 0x0F;
            // m_MinSpatialSegmentation = AP4_BytesToUInt16BE(&payload[13]) & 0x0FFF;
            // m_Reserved2              = (payload[15]>>2) & 0x3F;
            // m_ParallelismType        = payload[15] & 0x03;
            // m_Reserved3              = (payload[16]>>2) & 0x3F;
            // m_ChromaFormat           = payload[16] & 0x03;
            // m_Reserved4              = (payload[17]>>3) & 0x1F;
            // m_LumaBitDepth           = 8+(payload[17] & 0x07);
            // m_Reserved5              = (payload[18]>>3) & 0x1F;
            // m_ChromaBitDepth         = 8+(payload[18] & 0x07);
            // m_AverageFrameRate       = AP4_BytesToUInt16BE(&payload[19]);
            // m_ConstantFrameRate      = (payload[21]>>6) & 0x03;
            // m_NumTemporalLayers      = (payload[21]>>3) & 0x07;
            // m_TemporalIdNested       = (payload[21]>>2) & 0x01;
            track->nalu_length_size       = 1 + (payload[21] & 0x03);
            uint8_t num_seq_params = payload[22];
            uint32_t cursor = 23;
            for (uint32_t i = 0; i < num_seq_params; i++) {
                mp4_sequence seq;
                if (cursor + 1 > payload_size) break;
                seq.nalu_type          = payload[cursor] & 0x3F;
                cursor += 1;
                if (cursor + 2 > payload_size) break;
                uint16_t nalu_count = read_uint16_be(&payload[cursor]);
                cursor += 2;
                for (uint32_t j = 0; j < nalu_count; j++) {
                    if (cursor+2 > payload_size) break;
                    nal_chunk chunk;
                    chunk.size = read_uint16_be(&payload[cursor]);
                    cursor += 2;
                    if (cursor + chunk.size > payload_size) break;
                    chunk.data = (uint8_t*)malloc(chunk.size);
                    memcpy(chunk.data, &payload[cursor], chunk.size);
                    cursor += chunk.size;
                    seq.nalus.push_back(chunk);
                }
                track->sequences_hevc.push_back(seq);
            }
        } else if(type == "mp4a") {
            fs->read(2);
            int version = fs->next_uint16();
            fs->skip(14);
            fs->read(2);
            track->num_channels = fs->next_uint16();
            fs->skip(4);
            fs->read(4);
            track->sample_rate = fs->next_uint32();
            if(version == 1) { // ! untested
                fs->skip(16);
            } else if(version == 2) { // ! untested
                fs->read(4);
                int ext_size = fs->next_uint32();
                fs->skip(32);
                if(ext_size > 72) {
                    fs->skip(ext_size - 72);
                }
            } else {
                fs->skip(2);
            }
        } else if(type == "esds") {
            fs->skip(4);
            fs->read(1);
            int tag = fs->get();

            // bento 4: so tricky to get the payload size
            uint32_t      payload_size = 0;
            unsigned int  header_size = 1;
            unsigned int  max  = 4;
            unsigned char ext  = 0;
            do {
                header_size++;
                fs->read(1);
                ext = fs->get();
                payload_size = (payload_size << 7) + (ext & 0x7F);
            } while (--max && (ext & 0x80));
            if(tag == 3) {
                uint8_t* payload = (uint8_t*)malloc(payload_size);
                fs->read(payload, payload_size);
                int es_id = read_uint16_be(payload);
                int bits = payload[2];
                free(payload);
            }
        }
    }

    bool is_track_atom = type == "trak";
    if(
        type == "moov" ||
        is_track_atom  ||
        type == "mdia" ||
        type == "stsd" ||
        type == "minf" ||
        type == "avc1" || type == "avc2" || type == "avc3" || type == "avc4" ||
        type == "hvc1" ||
        type == "stbl" ||
        type == "mp4a") { // has children
        int readed_offset = fs->count;
        while(readed_offset < size) {
            fs->set(offset + readed_offset);
            int result = parse_mp4_box(fs, readed_offset + offset, level + 1, mp4, is_track_atom || read_track, verbose);
            if(result == -2 && is_track_atom) {
                printf("ignore track\n");
                // TODO: untested
                mp4->tracks.pop_back(); // remove unused track
            }
            readed_offset += result;
        }
        if(type == "moov") {
            return -1;
        }
    } else { // jump to next atom
        fs->set(offset + size);
    }
    return size;
}

struct mp4_file* mp4_open(const char* filename, bool verbose) {
    file_stream* fs = new file_stream(filename);
    int offset = 0;
    mp4_file* mp4 = new mp4_file;
    while(true) {
        int result = parse_mp4_box(fs, offset, 0, mp4, false, verbose);
        if(result == -1) {
            break;
        } else {
            offset += result;
        }
    }
    for(mp4_track &track : mp4->tracks) {
        if(track.type != VIDEO_TRACK) {
            continue;
        }
        fs->set(track.chunk_offsets[0] + track.nalu_length_size);
        fs->read(1);
        track.begin_payload_packet = fs->get();
    }
    mp4->fs = fs;
    return mp4;
}

void mp4_get_video_sample(struct mp4_track* track, int index, mp4_sample &sample) {
    {
        index++;
        int chunk, skip_samples;
        size_t group = 0;
        while(group < track->samples_groups.size()) {
            mp4_chunk_group cur_group = track->samples_groups[group];
            int samples_count = cur_group.chunk_count * cur_group.samples_per_chunk;
            if(samples_count && cur_group.first_sample + samples_count <= index) {
                group++;
                continue;
            }
            int chunk_offset = (index - cur_group.first_sample) / cur_group.samples_per_chunk;
            chunk = cur_group.first_chunk + chunk_offset;
            skip_samples = index - (cur_group.first_sample + cur_group.samples_per_chunk*chunk_offset);
            break;
        }
        
        int offset = track->chunk_offsets[chunk - 1];
        for(int i = (index - skip_samples); i < index; i++) {
            offset += track->samples_sizes[i - 1];
        }
        sample.offset = offset;
        sample.size = track->samples_sizes[index - 1];
    }
}


void mp4_nearest_iframe(struct mp4_file* mp4, struct mp4_track* track, int index, int* result) {
    *result = -1;
    mp4_sample sample;

    // backward pass
    for(int i = index; i >= 0; i--) {
        mp4_get_video_sample(track, i, sample);
        mp4->fs->set(sample.offset + track->nalu_length_size);
        mp4->fs->read(1);
        if(mp4->fs->get() == track->begin_payload_packet) {
            *result = i;
            break;
        }
    }
}

void mp4_read_video_sample(struct mp4_file* mp4, struct mp4_track* track, int index, mp4_sample &sample, bool include_nal_unit) {
    struct file_stream* fs = mp4->fs;
    mp4_get_video_sample(track, index, sample);
    fs->set(sample.offset);
    if(include_nal_unit) {
        int offset = 0;
        int packet_size = 0;
        bool nalu_in_elementary_data = track->bs_type == H264_AVC3 || track->bs_type == H264_AVC4;
        
        // calculate NAL packet size
        while(offset < sample.size) {
            int nalu_size = 0;
            fs->read(track->nalu_length_size);
            if(track->nalu_length_size == 4) {
                nalu_size = fs->next_uint32();
            } else if(track->nalu_length_size == 2) {
                nalu_size = fs->next_uint16();
            }
            offset += track->nalu_length_size;
            fs->read(1);
            if(fs->get() == track->begin_payload_packet && !nalu_in_elementary_data) {
                if(track->bs_type == H265_HEVC) {
                    for(int i = 0; i < track->sequences_hevc.size(); i++) {
                        mp4_sequence seq = track->sequences_hevc[i];
                        for(int i = 0; i < seq.nalus.size(); i++) {
                            packet_size += (seq.nalus[i].size + 4);
                        }
                    }
                } else {
                    for(int i = 0; i < track->sequences_nal.size(); i++) {
                        packet_size += (track->sequences_nal[i].size + 4);
                    }
                    for(int i = 0; i < track->pictures_nal.size(); i++) {
                        packet_size += (track->pictures_nal[i].size + 4);
                    }
                }
                packet_size += 3;
            } else {
                packet_size += (offset == track->nalu_length_size ? 4 : 3);
            }
            if(nalu_size - 1 > 0) {
                fs->skip(nalu_size - 1);
            }
            offset += nalu_size;
            packet_size += nalu_size;
        }
    
        // build NAL packet
        sample.data = (uint8_t*)malloc(packet_size);
        int packet_offset = 0;
        offset = 0;
        fs->set(sample.offset);
        while(offset < sample.size) {
            int nalu_size = 0;
            fs->read(track->nalu_length_size);
            if(track->nalu_length_size == 4) {
                nalu_size = fs->next_uint32();
            } else if(track->nalu_length_size == 2) {
                nalu_size =fs->next_uint16();
            }
            offset += track->nalu_length_size;
            fs->read(1);
            fs->set(sample.offset + offset);
            if(fs->get() == track->begin_payload_packet && !nalu_in_elementary_data) {
                if(track->bs_type == H265_HEVC) {
                    // VPS packet
                    for(int i = 0; i < track->sequences_hevc.size(); i++) {
                        mp4_sequence seq = track->sequences_hevc[i];
                        if(seq.nalu_type == HEVC_NALU_VPS) {
                            for(int i = 0; i < seq.nalus.size(); i++) {
                                nal_chunk chunk = seq.nalus[i];
                                memcpy(&sample.data[packet_offset], &mp4->nal_head, 4);
                                packet_offset += 4;
                                memcpy(&sample.data[packet_offset], chunk.data, chunk.size);
                                packet_offset += chunk.size;
                            }
                        }
                    }

                    // SPS packet
                    for(int i = 0; i < track->sequences_hevc.size(); i++) {
                        mp4_sequence seq = track->sequences_hevc[i];
                        if(seq.nalu_type == HEVC_NALU_SPS) {
                            for(int i = 0; i < seq.nalus.size(); i++) {
                                nal_chunk chunk = seq.nalus[i];
                                memcpy(&sample.data[packet_offset], &mp4->nal_head, 4);
                                packet_offset += 4;
                                memcpy(&sample.data[packet_offset], chunk.data, chunk.size);
                                packet_offset += chunk.size;
                            }
                        }
                    }

                    // PPS packet
                    for(int i = 0; i < track->sequences_hevc.size(); i++) {
                        mp4_sequence seq = track->sequences_hevc[i];
                        if(seq.nalu_type == HEVC_NALU_PPS) {
                            for(int i = 0; i < seq.nalus.size(); i++) {
                                nal_chunk chunk = seq.nalus[i];
                                memcpy(&sample.data[packet_offset], &mp4->nal_head, 4);
                                packet_offset += 4;
                                memcpy(&sample.data[packet_offset], chunk.data, chunk.size);
                                packet_offset += chunk.size;
                            }
                        }
                    }
                } else {
                    // SPS packet
                    for(int i = 0; i < track->sequences_nal.size(); i++) {
                        nal_chunk chunk = track->sequences_nal[i];
                        memcpy(&sample.data[packet_offset], &mp4->nal_head, 4);
                        packet_offset += 4;
                        memcpy(&sample.data[packet_offset], chunk.data, chunk.size);
                        packet_offset += chunk.size;
                    }

                    // PPS packet
                    for(int i = 0; i < track->pictures_nal.size(); i++) {
                        nal_chunk chunk = track->pictures_nal[i];
                        memcpy(&sample.data[packet_offset], &mp4->nal_head, 4);
                        packet_offset += 4;
                        memcpy(&sample.data[packet_offset], chunk.data, chunk.size);
                        packet_offset += chunk.size;
                    }
                }
                memcpy(&sample.data[packet_offset], &mp4->nal_body, 3);
                packet_offset += 3;
            } else {
                if(offset == track->nalu_length_size) {
                    memcpy(&sample.data[packet_offset], &mp4->nal_head, 4);
                    packet_offset += 4;
                } else {
                    memcpy(&sample.data[packet_offset], &mp4->nal_body, 3);
                    packet_offset += 3;
                }
            }
            fs->read(&sample.data[packet_offset], nalu_size);
            offset += nalu_size;
            packet_offset += nalu_size;
        }
        sample.size = packet_size;
    } else {
        sample.data = (uint8_t*)malloc(sample.size);
        fs->read(sample.data, sample.size);
    }
}

struct mp4_track* mp4_get_track(struct mp4_file* mp4, mp4_track_type type) {
    for(mp4_track &track : mp4->tracks) {
        if(track.type == type) {
            return &track;
        }
    }
    return NULL;
}