#pragma once
#include "openmp3.h"
#include <stdint.h>


//
//declarations

namespace OpenMP3
{

	struct ScaleFactorBandIndices		//Scale factor band indices,for long and short windows
	{
		UInt l[23];
		UInt s[14];
	};

	
	extern const UInt kBitRates[15];

	extern const UInt kSampleRates[3];


	extern const ScaleFactorBandIndices kScaleFactorBandIndices[3];

	extern const UInt kScaleFactorSizes[16][2];


	extern const UInt8 kInfo[4];

}

const OpenMP3::UInt kScaleFactorSizes[16][2] =
{
	{ 0,0 },{ 0,1 },{ 0,2 },{ 0,3 },{ 3,0 },{ 1,1 },{ 1,2 },{ 1,3 },
	{ 2,1 },{ 2,2 },{ 2,3 },{ 3,1 },{ 3,2 },{ 3,3 },{ 4,2 },{ 4,3 }
};

const uint32_t bitrates_layer3_list[15] = {
    0, 32000, 40000, 48000, 56000,
    64000, 80000, 96000, 112000,
    128000, 160000, 192000, 224000, 256000, 320000
};

const uint32_t  samplerates_layer3_list[3] = { 44100, 48000, 32000 };
