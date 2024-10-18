#pragma once

#include "types.h"




//
//decl

namespace OpenMP3
{

	UInt ReadHuffman(Reservoir & br, UInt sfreq, const mp3_frame_data & data, UInt part_2_start, UInt gr, UInt ch, Float32 is[576]);

}
