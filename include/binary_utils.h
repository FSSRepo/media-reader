#include <stdint.h>

inline uint32_t read_uint32_be(const uint8_t* bytes)
{
    return
        ( ((uint32_t)bytes[0]) << 24 ) |
        ( ((uint32_t)bytes[1]) << 16 ) |
        ( ((uint32_t)bytes[2]) << 8  ) |
        ( ((uint32_t)bytes[3])       );
}

inline uint16_t read_uint16_be(const uint8_t* bytes)
{
    return
        ( ((uint16_t)bytes[0]) << 8  ) |
        ( ((uint16_t)bytes[1])       );
}

inline uint64_t read_uint64_be(const uint8_t* bytes)
{
    return
        ( ((uint64_t)bytes[0]) << 56 ) |
        ( ((uint64_t)bytes[1]) << 48 ) |
        ( ((uint64_t)bytes[2]) << 40 ) |
        ( ((uint64_t)bytes[3]) << 32 ) |
        ( ((uint64_t)bytes[4]) << 24 ) |
        ( ((uint64_t)bytes[5]) << 16 ) |
        ( ((uint64_t)bytes[6]) << 8  ) |
        ( ((uint64_t)bytes[7])       );
}