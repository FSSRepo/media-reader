#pragma once

#define C_PI 3.14159265358979323846

//
//declarations

namespace OpenMP3
{

	//enums
	
	enum Mode
	{
		kModeStereo,
		kModeJointStereo,
		kModeDualMono,
		kModeMono
	};



	//integral types

	typedef unsigned char UInt8;

	typedef unsigned short UInt16;

	typedef unsigned int UInt32;

	typedef UInt32 UInt;


	typedef float Float32;

};

struct mp3_frame_data {
    unsigned main_data_begin;         /* 9 bits */


	//side

	unsigned scfsi[2][4];             /* 1 bit */
	unsigned part2_3_length[2][2];    /* 12 bits */
	unsigned big_values[2][2];        /* 9 bits */
	OpenMP3::Float32 global_gain[2][2];       /* 8 bits */
	unsigned scalefac_compress[2][2]; /* 4 bits */

	bool window_switching[2][2];
	bool mixed_block[2][2];

	unsigned block_type[2][2];        /* 2 bits */
	unsigned table_select[2][2][3];   /* 5 bits */
	OpenMP3::Float32 subblock_gain[2][2][3];  /* 3 bits */
									  /* table_select[][][] */
	unsigned region0_count[2][2];     /* 4 bits */
	unsigned region1_count[2][2];     /* 3 bits */
									  /* end */
	unsigned preflag[2][2];           /* 1 bit */
	unsigned scalefac_scale[2][2];    /* 1 bit */
	unsigned count1table_select[2][2];/* 1 bit */

	unsigned count1[2][2];            //calc: by huff.dec.!


	//main

	unsigned scalefac_l[2][2][21];    /* 0-4 bits */
	unsigned scalefac_s[2][2][12][3]; /* 0-4 bits */

	float is[2][2][576];               //calc: freq lines
};