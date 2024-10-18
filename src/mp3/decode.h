#ifndef __DECODE_H__
#define __DECODE_H__
#include <stdint.h>
#include "types.h"
#include "mp3.h"

int mp3_decode(mp3_frame &frame, float output[2][1152], OpenMP3::Reservoir & br);

#endif