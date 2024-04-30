#include "media-reader.h"
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

// ffmpeg h264 decoder
extern "C" {
	#include <stdio.h>
	#include <stdlib.h>
	#include "libavcodec/avcodec.h"
	extern AVCodec ff_h264_decoder;
}

// libde265
#include "de265.h"

static void yuv_save(unsigned char *buf[], int wrap[], int xsize, int ysize, FILE *f)
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
							AVFrame *frame, int *frame_index, AVPacket *pkt, int flush, int write_start)
{
	int got_frame = 0;
	do {
		int len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
		if (len < 0) {
			fprintf(stderr, "Error while decoding frame %d\n", *frame_index);
			return len;
		}
		if (got_frame) {
			if (file && *frame_index >= write_start) {
				yuv_save(frame->data, frame->linesize, frame->width, frame->height, file);
			}
			printf("decoding %d\n", *frame_index);
			(*frame_index)++;
		}
	} while (flush && got_frame);
	return 0;
}

void print_usage() {
	printf("\nusage: main <mp4 file path> <options>\n\noptions:\n	-d <output file> <frame count> <frame start> : decode to yuv file\n	-e <output file> : extract bitstream data\n");
}

int main(int argc, char* argv[]) {
	// mp3_open("D:\\proyectos\\OpenMP3-master\\build\\bin\\Release\\test2.mp3");
    if (argc > 1) {
		mp4_file* mp4 = mp4_open(argv[1], false);
		printf("MP4 file information\n");
		for(mp4_track track : mp4->tracks) {
			if(track.type == VIDEO_TRACK) {
				printf("\nVideo Track:\n	width: %d\n	height: %d\n	frames per second: %.2f (%d frames)\n	nalu size: %d (byte start: 0x%X) \n	codec: %s\n",
					track.width,
					track.height,
					track.fps, track.sample_count,
					track.nalu_length_size, track.begin_payload_packet, track.bs_type == H265_HEVC ? "h.265 (HEVC)" : "h.264 (AVC)");
			} else if(track.type == AUDIO_TRACK) {
				printf("\nAudio Track:\n	sample count: %d\n	sample rate: %d Hz\n	channel: %s\n",
					track.sample_count,
					track.sample_rate,
					track.num_channels ? "stereo" : "mono");
			}
			printf("	duration: %.2f segs\n", track.duration * 1.0f / track.time_scale);
		}
		if(argc > 3) {
			std::string op = std::string(argv[2]);
			if(op == "-d" && argc >= 4) {
				mp4_track* video_track = mp4_get_track(mp4, VIDEO_TRACK);
				if(!video_track) {
					fprintf(stderr, "missing video track");
					return 0;
					
				}
				
				int frame_count = argc > 4 ? atoi(argv[4]) : video_track->sample_count;
				int frame_start = argc > 5 ? atoi(argv[5]) : 0;
				bool add_nal_header = true;
				mp4_sample sample;

				FILE *outfile = fopen(argv[3], "wb");
				if(video_track->bs_type != H265_HEVC) {
					avcodec_register(&ff_h264_decoder);
					AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
					if (!codec) {
						fprintf(stderr, "Codec not found\n");
						exit(1);
					}
					AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
					if (!codec_ctx) {
						fprintf(stderr, "Could not allocate video codec context\n");
						exit(1);
					}
					
					if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
						fprintf(stderr, "Could not open codec\n");
						exit(1);
					}

					AVFrame *frame = av_frame_alloc();
					if (!frame) {
						fprintf(stderr, "Could not allocate video frame\n");
						exit(1);
					}
					
					AVPacket packet;
					printf("decoding %d frames ...\n", frame_count);
					int nearest_packet_frame = 0;

					if(frame_start) {
						mp4_nearest_iframe(mp4, video_track, frame_start, &nearest_packet_frame);
						if(nearest_packet_frame == -1) {
							return 0;
						}
						printf("decoding from: %d to %d\n", nearest_packet_frame, frame_start);
					}

					int frame_index = nearest_packet_frame;

					for(int s = nearest_packet_frame; s < (frame_start + frame_count); s++) {
						mp4_read_video_sample(mp4, video_track, s, sample, add_nal_header);

						av_init_packet(&packet);
						packet.data = sample.data;
						packet.size = sample.size;

						int ret = decode_write_frame(outfile, codec_ctx, frame, &frame_index, &packet, 0, frame_start);
						if (ret < 0) {
							fprintf(stderr, "Decode or write frame error\n");
							exit(1);
						}
					}

					// Flush the decoder
					packet.data = NULL;
					packet.size = 0;
					decode_write_frame(outfile, codec_ctx, frame, &frame_index, &packet, 1, frame_start);

					avcodec_close(codec_ctx);
					av_free(codec_ctx);
					av_frame_free(&frame);
				} else {
					de265_error err = DE265_OK;
					de265_decoder_context *ctx = de265_new_decoder();

					de265_set_parameter_bool(ctx, DE265_DECODER_PARAM_BOOL_SEI_CHECK_HASH, false);
					de265_set_parameter_bool(ctx, DE265_DECODER_PARAM_SUPPRESS_FAULTY_PICTURES, false);

					de265_set_parameter_bool(ctx, DE265_DECODER_PARAM_DISABLE_DEBLOCKING, 0);
					de265_set_parameter_bool(ctx, DE265_DECODER_PARAM_DISABLE_SAO, 0);

					de265_set_limit_TID(ctx, 100);

					// multi-thread decoder
					// err = de265_start_worker_threads(ctx, 6);

					de265_set_verbosity(0);
					printf("decoding %d frames ...\n", frame_count);
					int nearest_packet_frame = 0;

					if(frame_start) {
						mp4_nearest_iframe(mp4, video_track, frame_start, &nearest_packet_frame);
						if(nearest_packet_frame == -1) {
							return 0;
						}
						printf("decoding from: %d to %d\n", nearest_packet_frame, frame_start);
					}

					int frame_index = nearest_packet_frame;
					int pos = 0;
					int more = 1;

					int s = nearest_packet_frame;
					bool stop = false;
					while(!stop) {
						if(s < (frame_start + frame_count)) {
							// feed more data to decoder
							mp4_read_video_sample(mp4, video_track, s, sample, add_nal_header);
							err = de265_push_data(ctx, sample.data, sample.size, pos, (void *)2);
							pos += sample.size;
							s++;
							if (err != DE265_OK) {
								break;
							}
						} else {
							err = de265_flush_data(ctx);
							stop = true;
						}

						more = 1;
						while(more) {
							more = 0;
							err = de265_decode(ctx, &more);
							if (err != DE265_OK) {
								// printf("error: %s\n", de265_get_error_text(err));
								break;
							}
							const de265_image *img = de265_get_next_picture(ctx);
							if (img) {
								printf("decoding %d\n", frame_index);
								if(frame_index >= frame_start) {
									for (int c = 0; c < 3; c++)
									{
										int stride;
										const uint8_t *p = 	de265_get_image_plane(img, c, &stride);
										int width = 		de265_get_image_width(img, c);
										for (int y = 0; y < de265_get_image_height(img, c); y++) {
											fwrite(p + y * stride, width, 1, outfile);
										}
									}
									fflush(outfile);
								}
								frame_index++;
							}
						}
					}
				}
				printf("Done");
				fclose(outfile);
			} else if(op == "-e" && argc == 4) {
				printf("extracting data into: %s\n", argv[3]);
				FILE* fo = fopen(argv[3], "wb");
				mp4_track* video_track = mp4_get_track(mp4, VIDEO_TRACK);
				if(!video_track) {
					fprintf(stderr, "missing video track");
					return 0;
				}
				mp4_sample smpl;
				bool add_nal_header = true;
				for(int s = 0; s < video_track->sample_count; s++) {
					mp4_read_video_sample(mp4, video_track, s, smpl, add_nal_header);
					fwrite(smpl.data, 1, smpl.size, fo);
				}
				fclose(fo);
				printf("%s data extracted", video_track->bs_type == H265_HEVC ? "h.265" : "h.h264");
			} else {
				print_usage();
			}
		}
		
	} else {
		print_usage();
	}

	// {
	// 		FILE* yuv = fopen("out.yuv", "rb");
	// 		int width = 640, height = 360;
	// 		int buf_size = width*height*3 / 2;
	// 		uint8_t* buffer = new uint8_t[buf_size];
	// 		fread(buffer, 1, buf_size, yuv);
	// 		fclose(yuv);
	// 		uint8_t* rgb = new uint8_t[width * height * 3];
	// 		memset(rgb, 0, width * height * 3);
	// 		yuv420rgb(buffer, rgb, width, height);
	// 		stbi_write_png("out.png", width, height, 3, (const unsigned char *) rgb, 0,"None");
	// }
}