#include "decode.h"

uint32_t get_bits(uint32_t n, uint8_t* data, int & offset, int & idx) {
    uint32_t a;
    uint8_t* p = reinterpret_cast<uint8_t*>(&a);
    p[0] = data[offset + 3];
    p[1] = data[offset + 2];
    p[2] = data[offset + 1];
    p[3] = data[offset];
    a =     a << idx; //Remove bits already used
    a =     a >> (32 - n); //Remove bits after the desired bits
    offset += 	(idx + n) >> 3;
    idx =   	(idx + n) & 0x7;
    return a;
}

int mp3_decode(mp3_frame &frame, int16_t* output) {
    // read side info
    int num_channels = frame.mode == MODE_MONO ? 1 : 2;
    int sideinfo_size = frame.mode == MODE_MONO ? 17 : 32;

    printf("size info size: %d\n", sideinfo_size);
    mp3_frame_data fdata;
    int offset = 0, idx = 0;
    fdata.main_data_begin = get_bits(9, frame.data, offset, idx);
    get_bits(frame.mode == MODE_MONO ? 5 : 3, frame.data, offset, idx);
    for(int ch = 0; ch < num_channels; ch++) {
        for(int band = 0; band < 4; band++) {
            fdata.scfsi[ch][band] = get_bits(1, frame.data, offset, idx);
        }
    }
    for (int gr = 0; gr < 2; gr++)
	{
		for (int ch = 0; ch < num_channels; ch++)
		{
			fdata.part2_3_length[gr][ch] = get_bits(12, frame.data, offset, idx);

			fdata.big_values[gr][ch] = get_bits(9, frame.data, offset, idx);

			if (fdata.big_values[gr][ch] > 288) return false;

			fdata.global_gain[gr][ch] = float(get_bits(8, frame.data, offset, idx));

			fdata.scalefac_compress[gr][ch] = get_bits(4, frame.data, offset, idx);

			fdata.window_switching[gr][ch] = get_bits(1, frame.data, offset, idx) == 1;

			if (fdata.window_switching[gr][ch])
			{
				fdata.block_type[gr][ch] = get_bits(2, frame.data, offset, idx);

				fdata.mixed_block[gr][ch] = get_bits(1, frame.data, offset, idx) == 1;

				for (int region = 0; region < 2; region++) {
                    fdata.table_select[gr][ch][region] = get_bits(5, frame.data, offset, idx);
                }

				for (int window = 0; window < 3; window++) {
                    fdata.subblock_gain[gr][ch][window] = float(get_bits(3, frame.data, offset, idx));
                }

				if ((fdata.block_type[gr][ch] == 2) && (!fdata.mixed_block[gr][ch]))
				{
					fdata.region0_count[gr][ch] = 8; /* Implicit */
				}
				else
				{
					fdata.region0_count[gr][ch] = 7; /* Implicit */
				}

				/* The standard is wrong on this!!! */   /* Implicit */
				fdata.region1_count[gr][ch] = 20 - fdata.region0_count[gr][ch];
			}
			else
			{
				for (int region = 0; region < 3; region++){
                    fdata.table_select[gr][ch][region] = get_bits(5, frame.data, offset, idx);
                }

				fdata.region0_count[gr][ch] = get_bits(4, frame.data, offset, idx);
				fdata.region1_count[gr][ch] = get_bits(3, frame.data, offset, idx);
				fdata.block_type[gr][ch] = 0;  /* Implicit */
			}

			fdata.preflag[gr][ch] = get_bits(1, frame.data, offset, idx);
			fdata.scalefac_scale[gr][ch] = get_bits(1, frame.data, offset, idx);
			fdata.count1table_select[gr][ch] = get_bits(1, frame.data, offset, idx);
            printf("end\n");
		}
	}
}