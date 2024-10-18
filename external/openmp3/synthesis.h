#pragma once

#include "types.h"




//
//decl

namespace OpenMP3
{

	void Antialias(const mp3_frame_data & data, UInt gr, UInt ch, Float32 is[576]);

	void HybridSynthesis(const mp3_frame_data & data, UInt gr, UInt ch, Float32 store[32][18], Float32 is[576]);

	void FrequencyInversion(Float32 is[576]);

	void SubbandSynthesis(const mp3_frame_data & data, const Float32 is[576], Float32 v_vec[1024], Float32 output[576]);

}