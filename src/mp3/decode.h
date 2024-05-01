#ifndef __DECODE_H__
#define __DECODE_H__
#include <stdint.h>
#include "mp3.h"

struct mp3_frame_data {
    int16_t main_data_begin;         /* 9 bits */

    int8_t scfsi[2][4];             /* 1 bit */
	int16_t part2_3_length[2][2];    /* 12 bits */
	int16_t big_values[2][2];        /* 9 bits */
	float global_gain[2][2];       /* 8 bits */
	int8_t scalefac_compress[2][2]; /* 4 bits */

	bool window_switching[2][2];
	bool mixed_block[2][2];

	int8_t block_type[2][2];        /* 2 bits */
	int8_t table_select[2][2][3];   /* 5 bits */
	float subblock_gain[2][2][3];  /* 3 bits */
									  /* table_select[][][] */
	int8_t region0_count[2][2];     /* 4 bits */
	int8_t region1_count[2][2];     /* 3 bits */
									  /* end */
	int8_t preflag[2][2];           /* 1 bit */
	int8_t scalefac_scale[2][2];    /* 1 bit */
	int8_t count1table_select[2][2];/* 1 bit */

	int8_t count1[2][2];            //calc: by huff.dec.!

	//main

	int8_t scalefac_l[2][2][21];    /* 0-4 bits */
	int8_t scalefac_s[2][2][12][3]; /* 0-4 bits */
	float is[2][2][576];               //calc: freq lines
};

int mp3_decode(mp3_frame &frame, int16_t* output);

#endif