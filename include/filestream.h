#ifndef __FILE_STREAM_H__
#define __FILE_STREAM_H__
#include <stdio.h>
#include "binary_utils.h"

struct file_stream {
    FILE* file;
    uint8_t* temp_buffer;
    int offset;
    int count = 0;
    int file_size = 0;

    file_stream(const char* path) {
        file = fopen(path, "rb");
        {
            fseek(file, 0, SEEK_END);
            file_size = ftell(file);
            fseek(file, 0, SEEK_SET);
        }
        temp_buffer = new uint8_t[12];
        offset = 0;
    }

    void skip(int len) {
        fseek(file, len, SEEK_CUR);
        count += len;
    }

    void set(int pos) {
        fseek(file, pos, SEEK_SET);
        count = pos;
    }

    bool end_of_file(int attempt_to_read) {
        return count + attempt_to_read >= file_size;
    }

    void read(uint8_t* data, int len) {
        fread(data, 1, len, file);
        count += len;
    }

    void flush(int len) {
        count = 0;
        read(len);
    }

    void read(int len) {
        fread(temp_buffer, 1, len, file);
        offset = 0;
        count += len;
    }

    uint8_t get() {
        uint8_t x = temp_buffer[offset];
        offset ++;
        return x;
    }

    const char* get_string(int len) {
        temp_buffer[offset + len] = '\0';
        const char* x = (const char*)temp_buffer + offset;
        offset += len;
        return x;
    }

    // big endian
    uint16_t next_uint16() {
        uint16_t x = read_uint16_be(temp_buffer + offset);
        offset += 2;
        return x;
    }

    uint32_t next_uint32() {
        uint32_t x = read_uint32_be(temp_buffer + offset);
        offset += 4;
        return x;
    }

    uint64_t next_uint64() {
        uint64_t x = read_uint64_be(temp_buffer + offset);
        offset += 8;
        return x;
    }
};
#endif