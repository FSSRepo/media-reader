#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <map>
#include <string>
#include "media-reader.h"


inline uint32_t
readUint(const uint8_t* bytes)
{
    return
        ( ((uint32_t)bytes[0])<<24 ) |
        ( ((uint32_t)bytes[1])<<16 ) |
        ( ((uint32_t)bytes[2])<<8  ) |
        ( ((uint32_t)bytes[3])     );
}

inline uint16_t
readUint16(const uint8_t* bytes)
{
    return
        ( ((uint32_t)bytes[0])<<8  ) |
        ( ((uint32_t)bytes[1])     );
}

uint64_t
readUint64(const uint8_t* bytes)
{
    return
        ( ((uint64_t)bytes[0])<<56 ) |
        ( ((uint64_t)bytes[1])<<48 ) |
        ( ((uint64_t)bytes[2])<<40 ) |
        ( ((uint64_t)bytes[3])<<32 ) |
        ( ((uint64_t)bytes[4])<<24 ) |
        ( ((uint64_t)bytes[5])<<16 ) |
        ( ((uint64_t)bytes[6])<<8  ) |
        ( ((uint64_t)bytes[7])     );
}

int parse_mp4_box(FILE* file, uint8_t* tmp_buf, int offset, int level, MP4* mp4, bool verbose) {
    tmp_buf[8] = '\0';
    fread(tmp_buf, 1, 8, file);
    int size = readUint(tmp_buf);
    std::string type = std::string((const char*)tmp_buf + 4);
    if(verbose) {
        for(int i = 0; i < level; i++) {
            printf("    ");
        }
        printf("%s [%d bytes]\n", type.c_str(), size);
    }
    int readed = 0;
    if(!mp4->video_track) {
        if(type == "tkhd") {
            readed = 64;
            fread(tmp_buf, 1, 1, file);
            uint8_t version = tmp_buf[0];
            fseek(file, 3, SEEK_CUR);
            if(version) {
                fseek(file, 32, SEEK_CUR); readed += 32;
            } else {
                fseek(file, 20, SEEK_CUR); readed += 20;
            }
            fseek(file, 52, SEEK_CUR); // skip version + flags + dates
            fread(tmp_buf, 1, 8, file);
            mp4->width = readUint(tmp_buf) / 65536;
            mp4->height = readUint(tmp_buf + 4) / 65536;
        }
        if(type == "mdhd") {
            fread(tmp_buf, 1, 1, file);
            uint8_t version = tmp_buf[0];
            fseek(file, 3, SEEK_CUR);
            readed = 12;
            if(version) {
                fseek(file, 16, SEEK_CUR); readed += 20;
                fread(tmp_buf, 1, 12, file);
                mp4->time_scale = readUint(tmp_buf);
                mp4->duration = readUint64(tmp_buf + 4);
            } else {
                fseek(file, 8, SEEK_CUR); readed += 8;
                fread(tmp_buf, 1, 8, file);
                mp4->time_scale = readUint(tmp_buf);
                mp4->duration = readUint(tmp_buf + 4);
            }
        }
        if(type == "hdlr" && !mp4->media_type_video) {
            readed = 12; // assume version 0
            fseek(file, 8, SEEK_CUR); // skip version + flags + dates
            fread(tmp_buf, 1, 4, file);
            tmp_buf[4] = '\0';
            mp4->media_type_video = strcmp((const char*)tmp_buf, "vide") == 0;
        }
        if(type == "stsz") {
            readed = 12; // flags
            fseek(file, 4, SEEK_CUR); // skip version + flags + dates
            fread(tmp_buf, 1, 8, file);
            int sample_size = readUint(tmp_buf);
            mp4->sample_count = readUint(tmp_buf + 4);
            mp4->fps = (float)(mp4->sample_count / (mp4->duration * 1.0f / mp4->time_scale));
            if(sample_size == 0) {
                if((size - 12 - 8) != mp4->sample_count*4) {
                    exit(0);
                }
                for(int i = 0; i < mp4->sample_count; i++) {
                    fread(tmp_buf, 1, 4, file);
                    mp4->samples_sizes.push_back(readUint(tmp_buf));
                }
                readed += mp4->sample_count*4;
            }
        }
        if(type == "stsc") {
            readed = 8; // flags
            fseek(file, 4, SEEK_CUR); // skip version + flags + dates
            fread(tmp_buf, 1, 4, file);
            int entry_count = readUint(tmp_buf);
            if((size - 12 - 4) == entry_count*12) {
                int first_sample = 1;
                for(int i = 0; i < entry_count; i++) {
                    fread(tmp_buf, 1, 12, file);
                    sample_group group;
                    group.first_chunk = readUint(tmp_buf);
                    group.samples_per_chunk = readUint(tmp_buf + 4);
                    group.samples_desc_index = readUint(tmp_buf + 8);
                    if(i) {
                        sample_group* prev = &mp4->samples_groups[i - 1];
                        prev->chunk_count = group.first_chunk - prev->first_chunk;
                        first_sample += prev->chunk_count * prev->samples_per_chunk;
                    }
                    group.chunk_count = 0;
                    group.first_sample = first_sample;
                    mp4->samples_groups.push_back(group);
                }
                readed += entry_count*12;
            }
        }
        if(type == "stco") {
            readed = 8; // flags
            fseek(file, 4, SEEK_CUR); // skip version + flags + dates
            fread(tmp_buf, 1, 4, file);
            int entry_count = readUint(tmp_buf);
            if((size - 12 - 4) == entry_count*4) {
                for(int i = 0; i < entry_count; i++) {
                    fread(tmp_buf, 1, 4, file);
                    mp4->chunk_offsets.push_back(readUint(tmp_buf));
                }
                readed += entry_count*4;
            }
        }
        if(type == "stsd") {
            readed = 8; // flags
            fseek(file, 4, SEEK_CUR); // skip version + flags + dates
            fread(tmp_buf, 1, 4, file);
            int entry_count = readUint(tmp_buf);
            if(entry_count != 1) {
                printf("MP4 error");
                return -1;
            }
        }
        if(type == "avc1") { // sample descriptor
            readed = 78;
            fseek(file, 24, SEEK_CUR);
            fread(tmp_buf, 1, 4, file);
            uint16_t width = readUint16(tmp_buf);
            uint16_t height = readUint16(tmp_buf + 2);
            if(mp4->width != width || mp4->height != height) {
                printf("MP4 error");
                return -1;
            }
            fseek(file, 50, SEEK_CUR);
        }
        if(type == "avcC") { // sample descriptor
            readed = (size - 8);
            uint8_t* payload = new uint8_t[readed];
            fread(payload, 1, readed, file);
            // uint8_t _ConfigurationVersion  = payload[0];
            // uint8_t m_Profile              = payload[1];
            // uint8_t m_ProfileCompatibility = payload[2];
            // uint8_t m_Level                = payload[3];
            mp4->nalu_length_size       = 1 + (payload[4] & 3);
            uint8_t num_seq_params = payload[5] & 31;
            uint32_t cursor = 6;
            for (uint32_t i = 0; i < num_seq_params; i++) {
                if (cursor+2 <= readed) {
                    uint16_t param_length = readUint16(&payload[cursor]);
                    cursor += 2;
                    if (cursor + param_length <= readed) {
                        nal_chunk chunk;
                        chunk.data = new uint8_t[param_length];
                        chunk.size = param_length;
                        memcpy(chunk.data, &payload[cursor], param_length);
                        mp4->sequences_nal.push_back(chunk);
                        cursor += param_length;
                    }
                }
            }
            if (cursor >= readed) {
                return -1;
            }
            uint8_t num_pic_params = payload[cursor++];
            for (uint32_t i = 0; i < num_pic_params; i++) {
                if (cursor+2 <= readed) {
                    uint8_t param_length = readUint16(&payload[cursor]);
                    cursor += 2;
                    if (cursor + param_length <= readed) {
                        nal_chunk chunk;
                        chunk.data = new uint8_t[param_length];
                        chunk.size = param_length;
                        memcpy(chunk.data, &payload[cursor], param_length);
                        mp4->pictures_nal.push_back(chunk);
                        cursor += param_length;
                    }
                }
            }
        }
    }
    bool is_moov_atom = type == "moov";
    bool is_stbl_atom = type == "stbl";
    if(
        is_moov_atom ||
        type == "trak" ||
        type == "mdia" ||
        type == "stsd" && !mp4->video_track ||
        type == "minf" ||
        type == "avc1" ||
        is_stbl_atom) { // has children
        int readed_offset = readed + 8;
        while(readed_offset < size) {
            fseek(file, offset + readed_offset, SEEK_SET);
            int result = parse_mp4_box(file, tmp_buf, readed_offset + offset, level + 1, mp4, verbose);
            readed_offset += result;
        }
        mp4->moov = is_moov_atom;
        if(is_stbl_atom && mp4->media_type_video) { // ignore other tracks
            mp4->video_track = true;
            mp4->file = file;
        }
    } else { // saltar
        fseek(file, offset + size - readed, SEEK_SET);
    }
    return mp4->moov ? -1 : size;
}

struct MP4* read_mp4(const char* filename, bool verbose) {
    FILE* file = fopen(filename, "rb");
    uint8_t* temporal_buffer = new uint8_t[12];
    int offset = 0;
    MP4* mp4 = new MP4;
    while(true) {
        int result = parse_mp4_box(file, temporal_buffer, offset, 0, mp4, verbose);
        if(result == -1) {
            break;
        } else {
            offset += result;
        }
    }
    // read begin byte
    fseek(file, mp4->chunk_offsets[0] + mp4->nalu_length_size, SEEK_SET);
    fread(temporal_buffer, 1, 1, file);
    mp4->begin_payload_packet = temporal_buffer[0];
    printf("payload: 0x%X\n", mp4->begin_payload_packet);
    return mp4;
}

void get_sample_params(struct MP4* mp4, int index, data_sample &sample) {
    {
        index++;
        int chunk, skip_samples;
        size_t group = 0;
        while(group < mp4->samples_groups.size()) {
            sample_group cur_group = mp4->samples_groups[group];
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
        int offset = mp4->chunk_offsets[chunk - 1];
        for(int i = (index - skip_samples); i < index; i++) {
            offset += mp4->samples_sizes[i - 1];
        }
        sample.offset = offset;
        sample.size = mp4->samples_sizes[index - 1];
    }
}


void find_nearest_key_frame(struct MP4* mp4, int index, int* result) {
    *result = -1;
    data_sample sample;

    // backward pass
    for(int i = index; i >= 0; i--) {
        get_sample_params(mp4, i, sample);
        fseek(mp4->file, sample.offset + mp4->nalu_length_size, SEEK_SET);
        fread(mp4->tmp_buffer, 1, 1, mp4->file);
        if(mp4->tmp_buffer[0] == mp4->begin_payload_packet) {
            *result = i;
            break;
        }
    }
}

void read_sample(struct MP4* mp4, int index, data_sample &sample, bool include_nal_unit) {
    get_sample_params(mp4, index, sample);
    fseek(mp4->file, sample.offset, SEEK_SET);
    if(include_nal_unit) {
        int offset = 0;
        int packet_size = 0;

        // calculate NAL packet size
        while(offset < sample.size) {
            int nalu_size = 0;
            fread(mp4->tmp_buffer, 1, mp4->nalu_length_size, mp4->file);
            if(mp4->nalu_length_size == 4) {
                nalu_size = readUint(mp4->tmp_buffer);
            } else if(mp4->nalu_length_size == 2) {
                nalu_size = readUint16(mp4->tmp_buffer);
            }
            offset += mp4->nalu_length_size;
            fread(mp4->tmp_buffer, 1, 1, mp4->file);
            if(mp4->tmp_buffer[0] == mp4->begin_payload_packet) {
                for(int i = 0; i < mp4->sequences_nal.size(); i++) {
                    packet_size += (mp4->sequences_nal[i].size + 4);
                }
                for(int i = 0; i < mp4->pictures_nal.size(); i++) {
                    packet_size += (mp4->pictures_nal[i].size + 4);
                }
                packet_size += 3;
            } else {
                packet_size += (offset == mp4->nalu_length_size ? 4 : 3);
            }
            if(nalu_size - 1 > 0) {
                fseek(mp4->file, nalu_size - 1, SEEK_CUR);
            }
            offset += nalu_size;
            packet_size += nalu_size;
        }
        // build NAL packet
        sample.data = (uint8_t*)malloc(packet_size);
        int packet_offset = 0;
        offset = 0;
        fseek(mp4->file, sample.offset, SEEK_SET);
        while(offset < sample.size) {
            int nalu_size = 0;
            fread(mp4->tmp_buffer, 1, mp4->nalu_length_size, mp4->file);
            if(mp4->nalu_length_size == 4) {
                nalu_size = readUint(mp4->tmp_buffer);
            } else if(mp4->nalu_length_size == 2) {
                nalu_size = readUint16(mp4->tmp_buffer);
            }
            offset += mp4->nalu_length_size;
            fread(mp4->tmp_buffer, 1, 1, mp4->file);
            fseek(mp4->file, sample.offset + offset, SEEK_SET);
            if(mp4->tmp_buffer[0] == mp4->begin_payload_packet) {
                for(int i = 0; i < mp4->sequences_nal.size(); i++) {
                    nal_chunk chunk = mp4->sequences_nal[i];
                    memcpy(&sample.data[packet_offset], &mp4->nal_head, 4);
                    packet_offset += 4;
                    memcpy(&sample.data[packet_offset], chunk.data, chunk.size);
                    packet_offset += chunk.size;
                }
                for(int i = 0; i < mp4->pictures_nal.size(); i++) {
                    nal_chunk chunk = mp4->pictures_nal[i];
                    memcpy(&sample.data[packet_offset], &mp4->nal_head, 4);
                    packet_offset += 4;
                    memcpy(&sample.data[packet_offset], chunk.data, chunk.size);
                    packet_offset += chunk.size;
                }
                memcpy(&sample.data[packet_offset], &mp4->nal_body, 3);
                packet_offset += 3;
            } else {
                if(offset == mp4->nalu_length_size) {
                    memcpy(&sample.data[packet_offset], &mp4->nal_head, 4);
                    packet_offset += 4;
                } else {
                    memcpy(&sample.data[packet_offset], &mp4->nal_body, 3);
                    packet_offset += 3;
                }
            }
            fread(&sample.data[packet_offset], 1, nalu_size, mp4->file);
            offset += nalu_size;
            packet_offset += nalu_size;
        }
        sample.size = packet_size;
    } else {
        if(!sample.data) {
            sample.data = new uint8_t[sample.size];
        }
        fread(sample.data, 1, sample.size, mp4->file);
    }
}

int clamp(int x, int min, int max) {
    return x > max ? max : (x < min ? min : x);
}

// yuv I420 -> RGB888
void yuv420rgb(uint8_t* yuv, uint8_t* rgb, int width, int height) {
    // yuv I420 layout [y: [width*height], u: [width/2 * height/2], v: [width/2 * height/2]]
    // u, v need upsample (bilinear?)
    int width_half = (width / 2), height_half = (height / 2);

    uint8_t* y_p = yuv;
    uint8_t* u_p = yuv + width * height;
    uint8_t* v_p = u_p + width_half * height_half;

    int uv_x = 1;

    float mat[9] = {
            1.0f, 0.0f, 1.402f,
            1.0f, -0.34414f, -0.71414f,
            1.0f, 1.772f, 0.0f };
    
    int uv_offset = 128;
    int rgb_offset = 0;
    uint8_t* u_p1 = u_p + width_half;
    uint8_t* v_p1 = v_p + width_half;

    for(int y = 0; y < height; y++) {
        bool y0 = (y - 1) % 2 == 0;

        for(int x = 0; x < width; x++) {
            int lum = y_p[y * width + x];
            int u = 0;
            int v = 0;
            bool x0 = (x - 1) % 2 == 0;

            // inplace middle bilinear upscale x4
            if(y == 0 || y == height - 1) {
                if(x == 0) {
                    u = u_p[0];
                    v = v_p[0];
                    uv_x = 1;
                } else {
                    if(x == width - 1) {
                        u = u_p[width_half - 1];
                        v = v_p[width_half - 1];
                    } else if(x0) {
                        u = (u_p[uv_x - 1] * 3 + u_p[uv_x]) / 4;
                        v = (v_p[uv_x - 1] * 3 + v_p[uv_x]) / 4;
                    } else {
                        u = (u_p[uv_x - 1] + u_p[uv_x] * 3) / 4;
                        v = (v_p[uv_x - 1] + v_p[uv_x] * 3) / 4;
                        uv_x ++;
                    }
                }
            } else {
                if(x == 0) {
                    if(y0) {
                        u = (u_p[0] * 3 + u_p1[1]) / 4;
                        v = (v_p[0] * 3 + v_p1[1]) / 4;
                    } else {
                        u = (u_p[0] + u_p1[1] * 3) / 4;
                        v = (v_p[0] + v_p1[1] * 3) / 4;
                    }
                    uv_x = 1;
                } else if(x < width - 1) {
                    if(y0) {
                        if(x0) {
                            u = (u_p[uv_x - 1] * 9 + u_p[uv_x] * 3 + u_p1[uv_x - 1] * 3 + u_p1[uv_x]) / 16;
                            v = (v_p[uv_x - 1] * 9 + v_p[uv_x] * 3 + v_p1[uv_x - 1] * 3 + v_p1[uv_x]) / 16;
                        } else {
                            u = (u_p[uv_x - 1] * 3 + u_p[uv_x] * 9 + u_p1[uv_x - 1] + u_p1[uv_x] * 3) / 16;
                            v = (v_p[uv_x - 1] * 3 + v_p[uv_x] * 9 + v_p1[uv_x - 1] + v_p1[uv_x] * 3) / 16;
                            uv_x ++;
                        }
                    } else {
                        if(x0) {
                            u = (u_p[uv_x - 1] * 3 + u_p[uv_x] + u_p1[uv_x - 1] * 9 + u_p1[uv_x] * 3) / 16;
                            v = (v_p[uv_x - 1] * 3 + v_p[uv_x] + v_p1[uv_x - 1] * 9 + v_p1[uv_x] * 3) / 16;
                        } else {
                            u = (u_p[uv_x - 1] + u_p[uv_x] * 3 + u_p1[uv_x - 1] * 3 + u_p1[uv_x] * 9) / 16;
                            v = (v_p[uv_x - 1] + v_p[uv_x] * 3 + v_p1[uv_x - 1] * 3 + v_p1[uv_x] * 9) / 16;
                            uv_x ++;
                        }
                    }
                } else {
                    int end = width_half - 1;
                    if(y0) {
                        u = (u_p[end] * 3 + u_p1[end]) / 4;
                        v = (v_p[end] * 3 + v_p1[end]) / 4;
                    } else {
                        u = (u_p[end] + u_p1[end] * 3) / 4;
                        v = (v_p[end] + v_p1[end] * 3) / 4;
                        u_p += width_half;
                        v_p += width_half;
                        u_p1 += width_half;
                        v_p1 += width_half;
                    }
                }
            }

            u -= uv_offset;
            v -= uv_offset;

            int r = (int)(mat[0] * lum + mat[1] * u + mat[2] * v);
            int g = (int)(mat[3] * lum + mat[4] * u + mat[5] * v);
            int b = (int)(mat[6] * lum + mat[7] * u + mat[8] * v);

            rgb[rgb_offset] =     clamp(r, 0, 255);
            rgb[rgb_offset + 1] = clamp(g, 0, 255);
            rgb[rgb_offset + 2] = clamp(b, 0, 255);
            rgb_offset += 3;
        }
    }
}