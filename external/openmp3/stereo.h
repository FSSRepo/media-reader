#pragma once

#include "types.h"




//
//decl

namespace OpenMP3
{

	void Stereo(UInt sfreq, UInt8 joint_stereo_mode, const mp3_frame_data & data, unsigned gr, Float32 is[2][576]);

}



