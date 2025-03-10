#include "synthesis.h"

#include "types.h"
#include "tables.h"




//
//impl

namespace OpenMP3
{

	void IMDCT_Win(UInt blocktype, const Float32 in[18], Float32 out[36]);


	extern const Float32 kCS[8];

	extern const Float32 kCa[8];

	extern const Float32 kSynthDtbl[512];

}

void OpenMP3::Antialias(const mp3_frame_data & data, UInt gr, UInt ch, Float32 is[576])
{
	bool window_switching = data.window_switching[gr][ch];

	bool block_type_2 = data.block_type[gr][ch] == 2;

	bool mixed_block = data.mixed_block[gr][ch];


	// No antialiasing is done for short blocks
	if (window_switching && block_type_2 && !mixed_block) return;

	// Setup the limit for how many subbands to transform
	UInt sblim = (window_switching && block_type_2 && mixed_block) ? 2 : 32;

	for (UInt sb = 1; sb < sblim; sb++)
	{
		for (UInt i = 0; i < 8; i++)
		{
			UInt li = 18 * sb - 1 - i;

			UInt ui = 18 * sb + i;

			Float32 cs = kCS[i];

			Float32 ca = kCa[i];

			Float32 lb = is[li] * cs - is[ui] * ca;

			Float32 ub = is[ui] * cs+ is[li] * ca;

			is[li] = lb;

			is[ui] = ub;
		}
	}
}

void OpenMP3::HybridSynthesis(const mp3_frame_data & data, UInt gr, UInt ch, Float32 store[32][18], Float32 is[576])
{
	bool window_switching = data.window_switching[gr][ch];

	UInt block_type = data.block_type[gr][ch];

	bool mixed_block = data.mixed_block[gr][ch];

	Float32 rawout[36];

	for (UInt sb = 0; sb < 32; sb++) //Loop through all 32 subbands
	{
		UInt blocktype = ((window_switching == 1) && mixed_block && (sb < 2)) ? 0 : block_type;

		IMDCT_Win(blocktype, is + (sb * 18), rawout);	//inverse modified DCT and windowing

		for (UInt i = 0; i < 18; i++)	//Overlapp add with stored vector into main_data vector
		{
			is[sb * 18 + i] = rawout[i] + store[sb][i];

			store[sb][i] = rawout[i + 18];
		}
	}
}

void OpenMP3::FrequencyInversion(Float32 is[576])
{
	for (UInt sb = 1; sb < 32; sb += 2) for (UInt i = 1; i < 18; i += 2) is[sb * 18 + i] = -is[sb * 18 + i];
}

void OpenMP3::SubbandSynthesis(const mp3_frame_data & data, const Float32 is[576], Float32 v_vec[1024], Float32 out[576])
{
	static float sbs_n_win[64][32];

	static bool init = true;

	if (init)
	{
		for (UInt i = 0; i < 64; i++) for (UInt j = 0; j < 32; j++) sbs_n_win[i][j] = Float32(cos(Float64((16 + i) * (2 * j + 1)) * (C_PI / 64.0)));

		init = false;
	}

	Float32 u_vec[512], s_vec[32];

	for (UInt ss = 0; ss < 18; ss++)  //Loop through 18 samples in 32 subbands
	{
		for (UInt i = 1023; i > 63; i--)  v_vec[i] = v_vec[i - 64];	//Shift up the V vector 

		for (UInt i = 0; i < 32; i++) s_vec[i] = is[i * 18 + ss]; //Copy next 32 time samples to a temp vector

		for (UInt i = 0; i < 64; i++) //Matrix multiply input with n_win[][] matrix
		{ 
			Float32 sum = 0.0;

			for (UInt j = 0; j < 32; j++) sum += sbs_n_win[i][j] * s_vec[j];

			v_vec[i] = sum;
		}

		for (UInt i = 0; i < 8; i++)	//Build the U vector
		{ 
			for (UInt j = 0; j < 32; j++) // <<7 == *128
			{ 
				UInt i6 = i << 6;

				UInt i7 = i << 7;

				u_vec[i6 + j] = v_vec[i7 + j];

				u_vec[i6 + j + 32] = v_vec[i7 + j + 96];
			}
		}

		for (UInt i = 0; i < 512; i++) u_vec[i] *= kSynthDtbl[i];	//Window by u_vec[i] with kSynthDtbl[i]

		for (UInt i = 0; i < 32; i++)
		{
			Float32 sum = 0.0;

			for (UInt j = 0; j < 16; j++) sum += u_vec[(j << 5) + i];	//sum += u_vec[j*32 + i];

			out[(32 * ss) + i] = sum;
		}
	}
}

//Does inverse modified DCT and windowing
void OpenMP3::IMDCT_Win(UInt blocktype, const Float32 in[18], Float32 out[36])
{
	static Float32 imdct_win[4][36];

	static Float32 cos_n12[6][12];

	static Float32 cos_n36[18][36];

	static bool init = true;

	if (init)
	{ 
		for (UInt i = 0; i < 36; i++)  imdct_win[0][i] = Float32(sin((C_PI / 36.0) * (i + 0.5))); //0
		for (UInt i = 0; i < 18; i++)  imdct_win[1][i] = Float32(sin((C_PI / 36.0) * (i + 0.5))); //1
		for (UInt i = 18; i < 24; i++) imdct_win[1][i] = 1.0f;
		for (UInt i = 24; i < 30; i++) imdct_win[1][i] = Float32(sin((C_PI / 12.0) * (i + 0.5 - 18.0)));
		for (UInt i = 30; i < 36; i++) imdct_win[1][i] = 0.0f;
		for (UInt i = 0; i < 12; i++)  imdct_win[2][i] = Float32(sin((C_PI / 12.0) * (i + 0.5))); //2
		for (UInt i = 12; i < 36; i++) imdct_win[2][i] = 0.0f;
		for (UInt i = 0; i < 6; i++)   imdct_win[3][i] = 0.0f; //3
		for (UInt i = 6; i < 12; i++)  imdct_win[3][i] = Float32(sin((C_PI / 12.0) * (i + 0.5 - 6.0)));
		for (UInt i = 12; i < 18; i++) imdct_win[3][i] = 1.0f;
		for (UInt i = 18; i < 36; i++) imdct_win[3][i] = Float32(sin((C_PI / 36.0) * (i + 0.5)));

		for (UInt p = 0; p < 12; p++) for (UInt m = 0; m < 6; m++) cos_n12[m][p] = Float32(cos(C_PI / 24 * (2 * p + 1 + 6) * (2 * m + 1)));

		for (UInt p = 0; p < 36; p++) for (UInt m = 0; m < 18; m++)	cos_n36[m][p] = Float32(cos(C_PI / 72 * (2 * p + 1 + 18) * (2 * m + 1)));

		init = false;
	}

	//MemClear(out, sizeof(Float32) * 36);

	for (UInt i = 0; i < 36; i++) out[i] = 0.0;

	const float * blocktype_imdct_win = imdct_win[blocktype];

	if (blocktype == 2)	//3 short blocks, N = 12
	{
		const float * tin = in;
		
		for (UInt i = 0; i < 3; i++)
		{
			for (UInt p = 0; p < 12; p++)
			{
				Float32 sum = 0.0f;

				for (UInt m = 0; m < 6; m++)		//N / 2
				{
					//sum += tin[i + 3 * m] * cos(C_PI / 24 * (2 * p + 1 + 6) * (2 * m + 1));

					sum += tin[i + 3 * m] * cos_n12[m][p];
				}

				out[6 * i + p + 6] += Float32(sum) * blocktype_imdct_win[p];
			}
		}
	}
	else
	{
		//block_type != 2, N = 36

		for (UInt p = 0; p < 36; p++)
		{
			Float32 sum = 0.0f;

			for (UInt m = 0; m < 18; m++)	//N / 2
			{
				//sum += in[m] * cos(C_PI / 72 * (2 * p + 1 + 18) * (2 * m + 1));

				sum += in[m] * cos_n36[m][p];
			}

			out[p] = Float32(sum) * blocktype_imdct_win[p];
		}
	}
}




//
//data

const float OpenMP3::kCS[8] = { 0.857493, 0.881742, 0.949629, 0.983315, 0.995518, 0.999161, 0.999899, 0.999993 };

const float OpenMP3::kCa[8] = { -0.514496,-0.471732,-0.313377,-0.181913,-0.094574,-0.040966,-0.014199,-0.003700 };

const float OpenMP3::kSynthDtbl[512] =
{
	0.000000000,-0.000015259,-0.000015259,-0.000015259,
	-0.000015259,-0.000015259,-0.000015259,-0.000030518,
	-0.000030518,-0.000030518,-0.000030518,-0.000045776,
	-0.000045776,-0.000061035,-0.000061035,-0.000076294,
	-0.000076294,-0.000091553,-0.000106812,-0.000106812,
	-0.000122070,-0.000137329,-0.000152588,-0.000167847,
	-0.000198364,-0.000213623,-0.000244141,-0.000259399,
	-0.000289917,-0.000320435,-0.000366211,-0.000396729,
	-0.000442505,-0.000473022,-0.000534058,-0.000579834,
	-0.000625610,-0.000686646,-0.000747681,-0.000808716,
	-0.000885010,-0.000961304,-0.001037598,-0.001113892,
	-0.001205444,-0.001296997,-0.001388550,-0.001480103,
	-0.001586914,-0.001693726,-0.001785278,-0.001907349,
	-0.002014160,-0.002120972,-0.002243042,-0.002349854,
	-0.002456665,-0.002578735,-0.002685547,-0.002792358,
	-0.002899170,-0.002990723,-0.003082275,-0.003173828,
	0.003250122, 0.003326416, 0.003387451, 0.003433228,
	0.003463745, 0.003479004, 0.003479004, 0.003463745,
	0.003417969, 0.003372192, 0.003280640, 0.003173828,
	0.003051758, 0.002883911, 0.002700806, 0.002487183,
	0.002227783, 0.001937866, 0.001617432, 0.001266479,
	0.000869751, 0.000442505,-0.000030518,-0.000549316,
	-0.001098633,-0.001693726,-0.002334595,-0.003005981,
	-0.003723145,-0.004486084,-0.005294800,-0.006118774,
	-0.007003784,-0.007919312,-0.008865356,-0.009841919,
	-0.010848999,-0.011886597,-0.012939453,-0.014022827,
	-0.015121460,-0.016235352,-0.017349243,-0.018463135,
	-0.019577026,-0.020690918,-0.021789551,-0.022857666,
	-0.023910522,-0.024932861,-0.025909424,-0.026840210,
	-0.027725220,-0.028533936,-0.029281616,-0.029937744,
	-0.030532837,-0.031005859,-0.031387329,-0.031661987,
	-0.031814575,-0.031845093,-0.031738281,-0.031478882,
	0.031082153, 0.030517578, 0.029785156, 0.028884888,
	0.027801514, 0.026535034, 0.025085449, 0.023422241,
	0.021575928, 0.019531250, 0.017257690, 0.014801025,
	0.012115479, 0.009231567, 0.006134033, 0.002822876,
	-0.000686646,-0.004394531,-0.008316040,-0.012420654,
	-0.016708374,-0.021179199,-0.025817871,-0.030609131,
	-0.035552979,-0.040634155,-0.045837402,-0.051132202,
	-0.056533813,-0.061996460,-0.067520142,-0.073059082,
	-0.078628540,-0.084182739,-0.089706421,-0.095169067,
	-0.100540161,-0.105819702,-0.110946655,-0.115921021,
	-0.120697021,-0.125259399,-0.129562378,-0.133590698,
	-0.137298584,-0.140670776,-0.143676758,-0.146255493,
	-0.148422241,-0.150115967,-0.151306152,-0.151962280,
	-0.152069092,-0.151596069,-0.150497437,-0.148773193,
	-0.146362305,-0.143264771,-0.139450073,-0.134887695,
	-0.129577637,-0.123474121,-0.116577148,-0.108856201,
	0.100311279, 0.090927124, 0.080688477, 0.069595337,
	0.057617188, 0.044784546, 0.031082153, 0.016510010,
	0.001068115,-0.015228271,-0.032379150,-0.050354004,
	-0.069168091,-0.088775635,-0.109161377,-0.130310059,
	-0.152206421,-0.174789429,-0.198059082,-0.221984863,
	-0.246505737,-0.271591187,-0.297210693,-0.323318481,
	-0.349868774,-0.376800537,-0.404083252,-0.431655884,
	-0.459472656,-0.487472534,-0.515609741,-0.543823242,
	-0.572036743,-0.600219727,-0.628295898,-0.656219482,
	-0.683914185,-0.711318970,-0.738372803,-0.765029907,
	-0.791213989,-0.816864014,-0.841949463,-0.866363525,
	-0.890090942,-0.913055420,-0.935195923,-0.956481934,
	-0.976852417,-0.996246338,-1.014617920,-1.031936646,
	-1.048156738,-1.063217163,-1.077117920,-1.089782715,
	-1.101211548,-1.111373901,-1.120223999,-1.127746582,
	-1.133926392,-1.138763428,-1.142211914,-1.144287109,
	1.144989014, 1.144287109, 1.142211914, 1.138763428,
	1.133926392, 1.127746582, 1.120223999, 1.111373901,
	1.101211548, 1.089782715, 1.077117920, 1.063217163,
	1.048156738, 1.031936646, 1.014617920, 0.996246338,
	0.976852417, 0.956481934, 0.935195923, 0.913055420,
	0.890090942, 0.866363525, 0.841949463, 0.816864014,
	0.791213989, 0.765029907, 0.738372803, 0.711318970,
	0.683914185, 0.656219482, 0.628295898, 0.600219727,
	0.572036743, 0.543823242, 0.515609741, 0.487472534,
	0.459472656, 0.431655884, 0.404083252, 0.376800537,
	0.349868774, 0.323318481, 0.297210693, 0.271591187,
	0.246505737, 0.221984863, 0.198059082, 0.174789429,
	0.152206421, 0.130310059, 0.109161377, 0.088775635,
	0.069168091, 0.050354004, 0.032379150, 0.015228271,
	-0.001068115,-0.016510010,-0.031082153,-0.044784546,
	-0.057617188,-0.069595337,-0.080688477,-0.090927124,
	0.100311279, 0.108856201, 0.116577148, 0.123474121,
	0.129577637, 0.134887695, 0.139450073, 0.143264771,
	0.146362305, 0.148773193, 0.150497437, 0.151596069,
	0.152069092, 0.151962280, 0.151306152, 0.150115967,
	0.148422241, 0.146255493, 0.143676758, 0.140670776,
	0.137298584, 0.133590698, 0.129562378, 0.125259399,
	0.120697021, 0.115921021, 0.110946655, 0.105819702,
	0.100540161, 0.095169067, 0.089706421, 0.084182739,
	0.078628540, 0.073059082, 0.067520142, 0.061996460,
	0.056533813, 0.051132202, 0.045837402, 0.040634155,
	0.035552979, 0.030609131, 0.025817871, 0.021179199,
	0.016708374, 0.012420654, 0.008316040, 0.004394531,
	0.000686646,-0.002822876,-0.006134033,-0.009231567,
	-0.012115479,-0.014801025,-0.017257690,-0.019531250,
	-0.021575928,-0.023422241,-0.025085449,-0.026535034,
	-0.027801514,-0.028884888,-0.029785156,-0.030517578,
	0.031082153, 0.031478882, 0.031738281, 0.031845093,
	0.031814575, 0.031661987, 0.031387329, 0.031005859,
	0.030532837, 0.029937744, 0.029281616, 0.028533936,
	0.027725220, 0.026840210, 0.025909424, 0.024932861,
	0.023910522, 0.022857666, 0.021789551, 0.020690918,
	0.019577026, 0.018463135, 0.017349243, 0.016235352,
	0.015121460, 0.014022827, 0.012939453, 0.011886597,
	0.010848999, 0.009841919, 0.008865356, 0.007919312,
	0.007003784, 0.006118774, 0.005294800, 0.004486084,
	0.003723145, 0.003005981, 0.002334595, 0.001693726,
	0.001098633, 0.000549316, 0.000030518,-0.000442505,
	-0.000869751,-0.001266479,-0.001617432,-0.001937866,
	-0.002227783,-0.002487183,-0.002700806,-0.002883911,
	-0.003051758,-0.003173828,-0.003280640,-0.003372192,
	-0.003417969,-0.003463745,-0.003479004,-0.003479004,
	-0.003463745,-0.003433228,-0.003387451,-0.003326416,
	0.003250122, 0.003173828, 0.003082275, 0.002990723,
	0.002899170, 0.002792358, 0.002685547, 0.002578735,
	0.002456665, 0.002349854, 0.002243042, 0.002120972,
	0.002014160, 0.001907349, 0.001785278, 0.001693726,
	0.001586914, 0.001480103, 0.001388550, 0.001296997,
	0.001205444, 0.001113892, 0.001037598, 0.000961304,
	0.000885010, 0.000808716, 0.000747681, 0.000686646,
	0.000625610, 0.000579834, 0.000534058, 0.000473022,
	0.000442505, 0.000396729, 0.000366211, 0.000320435,
	0.000289917, 0.000259399, 0.000244141, 0.000213623,
	0.000198364, 0.000167847, 0.000152588, 0.000137329,
	0.000122070, 0.000106812, 0.000106812, 0.000091553,
	0.000076294, 0.000076294, 0.000061035, 0.000061035,
	0.000045776, 0.000045776, 0.000030518, 0.000030518,
	0.000030518, 0.000030518, 0.000015259, 0.000015259,
	0.000015259, 0.000015259, 0.000015259, 0.000015259,
};
