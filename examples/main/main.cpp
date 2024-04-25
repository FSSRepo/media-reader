#include "media-reader.h"

extern "C" {
	#include <stdio.h>
	#include <stdlib.h>
	#include "libavcodec/avcodec.h"
	extern AVCodec ff_h264_decoder;
}

static void yuv_save(unsigned char *buf[], int wrap[], int xsize,int ysize, FILE *f)
{
	int i;
	for (i = 0; i < ysize; i++) {
		fwrite(buf[0] + i * wrap[0], 1, xsize, f);
	}
	for (i = 0; i < ysize / 2; i++) {
		fwrite(buf[1] + i * wrap[1], 1, xsize/2, f);
	}
	for (i = 0; i < ysize / 2; i++) {
		fwrite(buf[2] + i * wrap[2], 1, xsize/2, f);
	}
}


static int decode_write_frame(FILE *file, AVCodecContext *avctx,
							AVFrame *frame, int *frame_index, AVPacket *pkt, int flush)
{
	int got_frame = 0;
	do {
		int len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
		if (len < 0) {
			fprintf(stderr, "Error while decoding frame %d\n", *frame_index);
			return len;
		}
		if (got_frame) {
			printf("Got frame %d - %dx%d\n", *frame_index, frame->width, frame->height);
			if (file) {
				yuv_save(frame->data, frame->linesize, frame->width, frame->height, file);
			}
			(*frame_index)++;
		}
	} while (flush && got_frame);
	return 0;
}


int main(int argc, char* argv[]) {
    if (argc == 2) {
		MP4* mp4 = read_mp4(argv[1], false);
		printf("mp4 file information\nresolution: %d x %d\nfps: %.2f, frames: %d\nNALU size: %d\nsequences: %zu\nduration: %.2f segs\n", mp4->width, mp4->height, mp4->fps, mp4->sample_count, mp4->nalu_length_size, mp4->sequences_nal.size(), mp4->duration * 1.0f / mp4->time_scale);
		// {
		//     FILE* fo = fopen("encoded_mine.h264", "wb");
		//     data_sample smpl;
		// 	bool add_nal_header = true;
		//     for(int s = 0; s < mp4->sample_count; s++) {
		//         read_sample(mp4, s, smpl, add_nal_header);
		//         fwrite(smpl.data, 1, smpl.size, fo);
		//     }
		//     fclose(fo);
		// 	printf("h264 data extracted");
		// }

		// {
		// 	FILE *outfile = fopen("output.yuv", "wb");
			
		// 	avcodec_register(&ff_h264_decoder);
			
		// 	AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
		// 	if (!codec) {
		// 		fprintf(stderr, "Codec not found\n");
		// 		exit(1);
		// 	}

		// 	AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
		// 	if (!codec_ctx) {
		// 		fprintf(stderr, "Could not allocate video codec context\n");
		// 		exit(1);
		// 	}
			
		// 	if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
		// 		fprintf(stderr, "Could not open codec\n");
		// 		exit(1);
		// 	}

		// 	AVFrame *frame = av_frame_alloc();
		// 	if (!frame) {
		// 		fprintf(stderr, "Could not allocate video frame\n");
		// 		exit(1);
		// 	}

		// 	bool add_nal_header = true;
		// 	data_sample sample;
		// 	AVPacket packet;
		// 	int frame_index = 0;

		//     for(int s = 0; s < mp4->sample_count; s++) {
		//         read_sample(mp4, s, sample, add_nal_header);

		//         av_init_packet(&packet);
		// 		packet.data = sample.data;
		// 		packet.size = sample.size;

		// 		int ret = decode_write_frame(outfile, codec_ctx, frame, &frame_index, &packet, 0);
		// 		if (ret < 0) {
		// 			fprintf(stderr, "Decode or write frame error\n");
		// 			exit(1);
		// 		}
		//     }

		// 	// Flush the decoder
		// 	packet.data = NULL;
		// 	packet.size = 0;
		// 	decode_write_frame(outfile, codec_ctx, frame, &frame_index, &packet, 1);

		// 	printf("Done");

		// 	fclose(outfile);

		// 	avcodec_close(codec_ctx);
		// 	av_free(codec_ctx);
		// 	av_frame_free(&frame);
		// }
	} else {
		printf("usage: main <mp4 file path>");
	}
}