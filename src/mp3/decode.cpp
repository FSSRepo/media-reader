#include "decode.h"
#include "requantize.h"
#include "stereo.h"
#include "synthesis.h"
#include "huffman.h"
#include "tables.h"

// based in https://github.com/audioboy77/OpenMP3/blob/master/src/decoder.cpp

inline uint32_t get_bits(uint32_t n, uint8_t* data, int & offset, int & idx) {
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

bool read_side_info(mp3_frame &frame, mp3_frame_data & fdata) {
	// read side info
    int num_channels = frame.mode == MODE_MONO ? 1 : 2;
    int sideinfo_size = frame.mode == MODE_MONO ? 17 : 32;
    int offset = 0, idx = 0;

    fdata.main_data_begin = get_bits(9, frame.data, offset, idx);

    get_bits(frame.mode == MODE_MONO ? 5 : 3, frame.data, offset, idx);

    for(int ch = 0; ch < num_channels; ch++) {
        for(int band = 0; band < 4; band++) {
            fdata.scfsi[ch][band] = get_bits(1, frame.data, offset, idx);
        }
    }
    for (int gr = 0; gr < 2; gr++) {
		for (int ch = 0; ch < num_channels; ch++) {
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
				if ((fdata.block_type[gr][ch] == 2) && (!fdata.mixed_block[gr][ch])) {
					fdata.region0_count[gr][ch] = 8;
				} else {
					fdata.region0_count[gr][ch] = 7;
				}
				// The standard is wrong on this!!! */
				fdata.region1_count[gr][ch] = 20 - fdata.region0_count[gr][ch];
			} else {
				for (int region = 0; region < 3; region++) {
                    fdata.table_select[gr][ch][region] = get_bits(5, frame.data, offset, idx);
                }

				fdata.region0_count[gr][ch] = get_bits(4, frame.data, offset, idx);
				fdata.region1_count[gr][ch] = get_bits(3, frame.data, offset, idx);
				fdata.block_type[gr][ch] = 0;
			}

			fdata.preflag[gr][ch] = get_bits(1, frame.data, offset, idx);
			fdata.scalefac_scale[gr][ch] = get_bits(1, frame.data, offset, idx);
			fdata.count1table_select[gr][ch] = get_bits(1, frame.data, offset, idx);
		}
	}
	frame.stream_data = frame.data + offset;
	return true;
}


inline void read_bytes_from_buffer(uint8_t* stream_buffer, OpenMP3::UInt no_of_bytes, OpenMP3::UInt data_vec[])
{
	//TODO this should return pointer to bytes, not upscale to UInt32
	//printf("DATABUFFER [byte 0: %d, byte 1: %d, byte 2: %d, byte 3: %d]\n", stream_buffer[0], stream_buffer[1], stream_buffer[2], stream_buffer[3]);
	for (OpenMP3::UInt i = 0; i < no_of_bytes; i++) {
		data_vec[i] = *stream_buffer++;
	}
}

static bool read_main_data(mp3_frame & frame, OpenMP3::Reservoir & br, OpenMP3::UInt main_data_begin, OpenMP3::UInt main_data_size)
{
	if (main_data_begin > br.main_data_top)
	{
		/* No,there is not,so we skip decoding this frame,but we have to
		* read the main_data bits from the bitstream in case they are needed
		* for decoding the next frame. */
		read_bytes_from_buffer(frame.stream_data, main_data_size, &(br.main_data_vec[br.main_data_top]));

		/* Set up pointers */
		br.main_data_ptr = &(br.main_data_vec[0]);
		br.main_data_idx = 0;
		br.main_data_top += main_data_size;
		return false;    /* This frame cannot be decoded! */
	}

	/* Copy data from previous frames */
	for (OpenMP3::UInt i = 0; i < main_data_begin; i++) br.main_data_vec[i] = br.main_data_vec[br.main_data_top - main_data_begin + i];

	/* Read the main_data from file */
	read_bytes_from_buffer(frame.stream_data, main_data_size, br.main_data_vec + main_data_begin);

	/* Set up pointers */
	br.main_data_ptr = &(br.main_data_vec[0]);
	br.main_data_idx = 0;
	br.main_data_top = main_data_begin + main_data_size;

	return true;
}

bool read_main(mp3_frame &frame, mp3_frame_data & data, OpenMP3::Reservoir & br)
{
	OpenMP3::UInt nch = (frame.mode == MODE_MONO ? 1 : 2);

	OpenMP3::UInt sideinfo_size = (nch == 1 ? 17 : 32);

	OpenMP3::UInt main_data_size = frame.size - sideinfo_size;


	//Assemble the main data buffer with data from this frame and the previous two frames into a local buffer used by the Get_Main_Bits function
	//main_data_begin indicates how many bytes from previous frames that should be used
	if (!read_main_data(frame, br, data.main_data_begin, main_data_size)) return false; //This could be due to not enough data in reservoir

	br.hack_bits_read = 0;

	OpenMP3::UInt sfb;

	for (OpenMP3::UInt gr = 0; gr < 2; gr++)
	{
		auto scalefac_compress_gr = data.scalefac_compress[gr];

		auto scalefac_l_gr = data.scalefac_l[gr];

		auto scalefac_s_gr = data.scalefac_s[gr];

		for (OpenMP3::UInt ch = 0; ch < nch; ch++)
		{
			OpenMP3::UInt part_2_start = br.GetPosition();

			/* Number of bits in the bitstream for the bands */

			//OpenMP3::UInt slen1 = kScaleFactorSizes[data.scalefac_compress[gr][ch]][0];

			//OpenMP3::UInt slen2 = kScaleFactorSizes[data.scalefac_compress[gr][ch]][1];

			auto scalefactors = kScaleFactorSizes[scalefac_compress_gr[ch]];

			OpenMP3::UInt slen1 = scalefactors[0];

			OpenMP3::UInt slen2 = scalefactors[1];

			if (data.window_switching[gr][ch] && (data.block_type[gr][ch] == 2))
			{
				if (data.mixed_block[gr][ch]) for (sfb = 0; sfb < 8; sfb++) scalefac_l_gr[ch][sfb] = br.ReadBits(slen1);

				//for (sfb = 0; sfb < 12; sfb++)
				//{
				//	OpenMP3::UInt nbits = (sfb < 6) ? slen1 : slen2; //TODO optimise, slen1 for band 3-5, slen2 for 6-11

				//	for (OpenMP3::UInt win = 0; win < 3; win++) scalefac_s_gr[ch][sfb][win] = br.ReadBits(nbits);
				//}

				for (sfb = 0; sfb < 6; sfb++) for (OpenMP3::UInt win = 0; win < 3; win++) scalefac_s_gr[ch][sfb][win] = br.ReadBits(slen1);

				for (sfb = 6; sfb < 12; sfb++) for (OpenMP3::UInt win = 0; win < 3; win++) scalefac_s_gr[ch][sfb][win] = br.ReadBits(slen2);
			}
			else
			{ /* block_type == 0 if winswitch == 0 */
			  /* Scale factor bands 0-5 */
				if ((data.scfsi[ch][0] == 0) || (gr == 0))
				{
					for (sfb = 0; sfb < 6; sfb++) scalefac_l_gr[ch][sfb] = br.ReadBits(slen1);
				}
				else if ((data.scfsi[ch][0] == 1) && (gr == 1))
				{
					/* Copy scalefactors from granule 0 to granule 1 */
					for (sfb = 0; sfb < 6; sfb++) data.scalefac_l[1][ch][sfb] = data.scalefac_l[0][ch][sfb];
				}

				/* Scale factor bands 6-10 */
				if ((data.scfsi[ch][1] == 0) || (gr == 0))
				{
					for (sfb = 6; sfb < 11; sfb++) scalefac_l_gr[ch][sfb] = br.ReadBits(slen1);
				}
				else if ((data.scfsi[ch][1] == 1) && (gr == 1))
				{
					/* Copy scalefactors from granule 0 to granule 1 */
					for (sfb = 6; sfb < 11; sfb++) data.scalefac_l[1][ch][sfb] = data.scalefac_l[0][ch][sfb];
				}

				/* Scale factor bands 11-15 */
				if ((data.scfsi[ch][2] == 0) || (gr == 0))
				{
					for (sfb = 11; sfb < 16; sfb++) scalefac_l_gr[ch][sfb] = br.ReadBits(slen2);
				}
				else if ((data.scfsi[ch][2] == 1) && (gr == 1))
				{
					/* Copy scalefactors from granule 0 to granule 1 */
					for (sfb = 11; sfb < 16; sfb++) data.scalefac_l[1][ch][sfb] = data.scalefac_l[0][ch][sfb];
				}

				/* Scale factor bands 16-20 */
				if ((data.scfsi[ch][3] == 0) || (gr == 0))
				{
					for (sfb = 16; sfb < 21; sfb++) scalefac_l_gr[ch][sfb] = br.ReadBits(slen2);
				}
				else if ((data.scfsi[ch][3] == 1) && (gr == 1))
				{
					/* Copy scalefactors from granule 0 to granule 1 */
					for (sfb = 16; sfb < 21; sfb++) data.scalefac_l[1][ch][sfb] = data.scalefac_l[0][ch][sfb];
				}
			}

			data.count1[gr][ch] = OpenMP3::ReadHuffman(br, frame.m_sr_index, data, part_2_start, gr, ch, data.is[gr][ch]);
		}
	}

	//TODO read ancillary data here

	//OpenMP3::UInt bytes = (br.hack_bits_read / 8) + (br.hack_bits_read & 7 ? 1 : 0);

	//const OpenMP3::UInt8 * ptr = frame.m_ptr + sideinfo_size + data.main_data_begin + bytes;

	//size_t size = (frame.m_ptr + frame.m_datasize) - ptr;

	return true;
}

void decode_frame(mp3_frame & header, mp3_frame_data &data, OpenMP3::Float32 datais[2][2][576], float out[2][1152])
{

	OpenMP3::UInt sfreq = header.m_sr_index;
	auto & store = header.m_hs_store;
	auto & v_vec = header.m_sbs_v_vec;

	if (header.mode == MODE_MONO)
	{
		for (OpenMP3::UInt gr = 0; gr < 2; gr++)
		{
			auto & is = datais[gr][0];

			OpenMP3::Requantize(sfreq, data, gr, 0, is);
			OpenMP3::Reorder(sfreq, data, gr, 0, is);
			OpenMP3::Antialias(data, gr, 0, is);
			OpenMP3::HybridSynthesis(data, gr, 0, store[0], is);
			OpenMP3::FrequencyInversion(is);
			OpenMP3::SubbandSynthesis(data, is, v_vec[0], out[0] + (576 * gr));
		}

		memcpy(out[1], out[0], 1152 * sizeof(OpenMP3::Float32));
	}
	else
	{
		OpenMP3::UInt8 joint_stereo_mode = header.mode_extension;

		bool stereo_decoding = (header.mode == MODE_STEREO) && (joint_stereo_mode != 0);	//joint_stereo & (Intensity stereo | MS stereo)

		for (OpenMP3::UInt gr = 0; gr < 2; gr++)
		{
			for (OpenMP3::UInt ch = 0; ch < 2; ch++)
			{
				auto & is = datais[gr][ch];
				OpenMP3::Requantize(sfreq, data, gr, ch, is);
				OpenMP3::Reorder(sfreq, data, gr, ch, is);
			}

			if (stereo_decoding) OpenMP3::Stereo(sfreq, joint_stereo_mode, data, gr, datais[gr]);

			for (OpenMP3::UInt ch = 0; ch < 2; ch++)
			{
				auto & is = datais[gr][ch];
				OpenMP3::Antialias(data, gr, ch, is);
				OpenMP3::HybridSynthesis(data, gr, ch, store[ch], is);
				OpenMP3::FrequencyInversion(is);
				OpenMP3::SubbandSynthesis(data, is, v_vec[ch], out[ch] + (576 * gr));
			}
		}
	}
}

int mp3_decode(mp3_frame &frame, float output[2][1152], OpenMP3::Reservoir & br) {
	uint8_t* dbuffer = (uint8_t*)malloc(sizeof(mp3_frame_data));
	memset(dbuffer, 0, sizeof(mp3_frame_data));
	mp3_frame_data & fdata = *(mp3_frame_data*)(dbuffer);
	if(!read_side_info(frame, fdata)) return 0;
	// OpenMP3 library
	if(read_main(frame, fdata, br)) {
		decode_frame(frame, fdata, fdata.is, output);
		return 1152;
	}
	return 0;
}