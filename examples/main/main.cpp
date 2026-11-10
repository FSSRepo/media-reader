#include "media-reader.h"
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

#include "aacdecoder_lib.h"

// ffmpeg h264 decoder
extern "C" {
	#include <stdio.h>
	#include <stdlib.h>
	#include "libavcodec/avcodec.h"
	extern AVCodec ff_h264_decoder;
}

#define PCM_BUFFER_SIZE 2 * 2048

#ifdef MR_USE_OGL_PLAYER
#include "glad/gl.h"
#include <GLFW/glfw3.h>
#define MIN(a,b) a < b ? a : b
#endif

#ifdef MR_USE_AUDIO_PLAYER
#include "miniaudio.h"
#endif

#if defined(MR_USE_AUDIO_PLAYER) || defined(MR_USE_OGL_PLAYER)
#include <thread>
#include <atomic>
#endif
#ifdef MR_USE_AUDIO_PLAYER
#include <mutex>
#include <condition_variable>
#include <deque>
#include <algorithm>
#endif

#if defined(MR_USE_AUDIO_PLAYER) && defined(MR_USE_OGL_PLAYER)
struct audio_stream_state {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<ma_int16> samples;
    bool ready = false;
    bool stop = false;
    bool device_initialized = false;
    ma_uint32 channels = 0;
    ma_uint32 sample_rate = 0;
    ma_device device;
};

static void audio_stream_callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    audio_stream_state* state = static_cast<audio_stream_state*>(device->pUserData);
    ma_int16* dst = static_cast<ma_int16*>(output);
    const size_t count = static_cast<size_t>(frames) * state->channels;
    std::lock_guard<std::mutex> lock(state->mutex);
    size_t available = std::min(count, state->samples.size());
    for (size_t i = 0; i < available; ++i) {
        dst[i] = state->samples.front();
        state->samples.pop_front();
    }
    std::fill(dst + available, dst + count, 0);
    state->changed.notify_one();
}
#endif

// libde265
#include "de265.h"

static void wav_write(audio_data* aud, const char* filename = "output.wav") {
	FILE* wav = fopen(filename, "wb");
	if (!wav) {
		fprintf(stderr, "Cannot open %s for writing\n", filename);
		return;
	}
	uint8_t wav_header[44] = {0};
	memcpy(wav_header, "RIFF", 4);
	uint16_t channels_ = (aud->stereo ? 2 : 1);
	uint32_t file_size_ = 36 + aud->data_size;
	memcpy(wav_header + 4, &file_size_, 4);
	memcpy(wav_header + 8, "WAVEfmt ", 8);
	uint32_t fmt_size = 16;
	memcpy(wav_header + 16, &fmt_size, 4);
	uint16_t audio_format = 1; // PCM
	memcpy(wav_header + 20, &audio_format, 2);
	memcpy(wav_header + 22, &channels_, 2);
	uint32_t sample_rate = aud->samplerate;
	memcpy(wav_header + 24, &sample_rate, 4);
	uint32_t byte_rate = aud->samplerate * channels_ * 2;
	memcpy(wav_header + 28, &byte_rate, 4);
	uint16_t block_align = channels_ * 2;
	memcpy(wav_header + 32, &block_align, 2);
	uint16_t bits_per_sample = 16;
	memcpy(wav_header + 34, &bits_per_sample, 2);
	memcpy(wav_header + 36, "data", 4);
	uint32_t data_size = aud->data_size;
	memcpy(wav_header + 40, &data_size, 4);
	fwrite(wav_header, 1, 44, wav);
	fwrite(aud->data, 1, aud->data_size, wav);
	fclose(wav);
	printf("WAV written to %s (%u bytes, %d Hz, %s)\n", filename, aud->data_size, aud->samplerate, aud->stereo ? "stereo" : "mono");
}

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

static int decode_write_frame(FILE *file, const char* filename, AVCodecContext *avctx,
							AVFrame *frame, int *frame_index, AVPacket *pkt, int flush, int write_start, bool save_png, uint8_t* rgb_buffer)
{
	int got_frame = 0;
	char png_filename[64];
	do {
		int len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
		if (len < 0) {
			fprintf(stderr, "Error while decoding frame %d\n", *frame_index);
			return len;
		}
		if (got_frame) {
			if (*frame_index >= write_start) {
				if(save_png) {
					yuv420rgb_strided(frame->data[0], frame->data[1], frame->data[2], frame->linesize[0], frame->linesize[1], rgb_buffer, frame->width, frame->height);
					snprintf(png_filename, sizeof(png_filename), filename, *frame_index);
					stbi_write_png(png_filename, frame->width, frame->height, 3, (const unsigned char *) rgb_buffer, 0, "None");
				} else {
					yuv_save(frame->data, frame->linesize, frame->width, frame->height, file);
				}
			}
			printf("decoding %d\n", *frame_index);
			(*frame_index)++;
		}
	} while (flush && got_frame);
	return 0;
}

void print_mp4_info(mp4_file* mp4) {
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
}

void decode_and_save(mp4_file* mp4, int argc, char* argv[], bool save_png) {
	mp4_track* video_track = mp4_get_track(mp4, VIDEO_TRACK);
	if(!video_track) {
		fprintf(stderr, "missing video track");
		exit(0);
	}
	
	int frame_count = argc > 4 ? atoi(argv[4]) : video_track->sample_count;
	int frame_start = argc > 5 ? atoi(argv[5]) : 0;
	bool add_nal_header = true;
	mp4_sample sample;

	FILE *outfile = NULL;
	uint8_t* rgb_buffer = NULL;
	if(!save_png) {
		outfile = fopen(argv[3], "wb");
	} else {
		rgb_buffer = new uint8_t[video_track->width * video_track->height * 3];
	}
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
				exit(0);
			}
			printf("decoding from: %d to %d\n", nearest_packet_frame, frame_start);
		}

		int frame_index = nearest_packet_frame;

		for(int s = nearest_packet_frame; s < (frame_start + frame_count); s++) {
			mp4_read_video_sample(mp4, video_track, s, sample, add_nal_header);

			av_init_packet(&packet);
			packet.data = sample.data;
			packet.size = sample.size;

			int ret = decode_write_frame(outfile, argv[3], codec_ctx, frame, &frame_index, &packet, 0, frame_start, save_png, rgb_buffer);
			if (ret < 0) {
				fprintf(stderr, "Decode or write frame error\n");
				exit(1);
			}
		}

		// Flush the decoder
		packet.data = NULL;
		packet.size = 0;
		decode_write_frame(outfile, argv[3], codec_ctx, frame, &frame_index, &packet, 1, frame_start, save_png, rgb_buffer);

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
				exit(0);
			}
			printf("decoding from: %d to %d\n", nearest_packet_frame, frame_start);
		}

		int frame_index = nearest_packet_frame;
		int pos = 0;
		int more = 1;
		char png_filename[64];
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
						if(save_png) {
							int stride, stride_uv;
							const uint8_t* plane_y = de265_get_image_plane(img, 0, &stride);
							const uint8_t* plane_u = de265_get_image_plane(img, 1, &stride_uv);
							yuv420rgb_strided(plane_y, plane_u, de265_get_image_plane(img, 2, &stride_uv), stride, stride_uv, rgb_buffer, video_track->width, video_track->height);
							snprintf(png_filename, sizeof(png_filename), argv[3], frame_index);
							stbi_write_png(png_filename, video_track->width, video_track->height, 3, (const unsigned char *) rgb_buffer, 0, "None");
						} else {
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
					}
					frame_index++;
				}
			}
		}
	}
	printf("Done");
	fclose(outfile);
}

void print_usage() {
	printf("\nusage: main <mp4 file path> <options>\n\noptions:\n\n	-d <output file> <frame count> <frame start> : decode to yuv file\n\n	-e <output file> : extract bitstream data\n\n	-c <output file (string formatted)> <frame count> <frame start> : extract as .png images\n\n		example: main test.mp4 -c out-%%d.png 10 0\n\n");
#ifdef MR_USE_OGL_PLAYER
	printf("	-p : play mp4 as an opengl texture (only h.264) / mp3 with miniaudio\n");
#endif
}

#ifdef MR_USE_OGL_PLAYER
const char* vertex_shader_code =
"#version 110\n"
"attribute vec4 aVertex;\n"
"varying vec2 vtex_coord;\n"
"void main() {\n"
"    gl_Position = vec4(aVertex.xy, 0.0, 1.0);\n"
"	 vtex_coord = aVertex.zw;\n"
"}\0";

const char* fragment_shader_code =
"#version 110\n"
"uniform sampler2D uTexture;\n"
"varying vec2 vtex_coord;\n"
"void main() {\n"
"    gl_FragColor = texture2D(uTexture, vtex_coord);\n"
"}\0";

int createShader(const char* code, int type) {
    int shader;
    shader = glCreateShader(type);
    glShaderSource(shader, 1, &code, NULL);
    glCompileShader(shader);
    int status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if(!status) {
        char info[512];
        glGetShaderInfoLog(shader, 512, NULL, info);
        printf("shader error: %s\n", info);
    }
    return shader;
}

void glfwError(int i, const char* des) {
    printf("%s\n", des);
}

struct video_state {
	std::atomic<int> video_frame{0};
	std::atomic<bool> load_new_frame{false};
	uint8_t* frame_data = NULL;
	std::atomic<bool> kill{false};

	void wait() {
		while(true) {
			if(load_new_frame) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}
};
#endif

void mp4_play(mp4_file* mp4, const char* filepath) {
#ifdef MR_USE_OGL_PLAYER
	mp4_track* video_track = mp4_get_track(mp4, VIDEO_TRACK);
	if(!video_track) {
		fprintf(stderr, "missing video track");
		exit(0);
	}
	glfwSetErrorCallback(&glfwError);
	if(!glfwInit())
	{
		printf("failed to initialize GLFW");
		return;
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	int window_width = MIN(video_track->width, 1280);
	int window_height = MIN(video_track->height, 720);

    GLFWwindow* window = glfwCreateWindow(window_width, window_height, "mp4 player", NULL, NULL);
    if(!window) {
        printf("failed to create GLFW window %d %d\n", window_width, window_height);
        return;
    }

    glfwMakeContextCurrent(window);

    if(!gladLoadGL(glfwGetProcAddress)) {
        printf("error loading opengl\n");
        glfwTerminate();
        return;
    }

    float lastTime = (float)glfwGetTime();

    const GLubyte* vendor =  glGetString(GL_VENDOR);
    const GLubyte* device =  glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    printf("OpenGL Vendor: %s\nOpenGL Device: %s\nOpenGL Version: %s\n", vendor, device, version);

	GLuint program;
	GLuint vbo, tex = -1;

#ifdef MR_USE_AUDIO_PLAYER
	mp4_track* audio_track = mp4_get_track(mp4, AUDIO_TRACK);
	bool have_audio = (audio_track != NULL);
	if(!have_audio) {
		fprintf(stderr, "missing audio track; will play video only\n");
	}
	audio_stream_state audio_state;
#else
	bool have_audio = false;
#endif

    {
		// create shaders
		int vertex_shader = createShader(vertex_shader_code, GL_VERTEX_SHADER);
		int fragment_shader = createShader(fragment_shader_code, GL_FRAGMENT_SHADER);

		program = glCreateProgram();
		glAttachShader(program, vertex_shader);
		glAttachShader(program, fragment_shader);
		glLinkProgram(program);

		int status;
		glGetProgramiv(program, GL_LINK_STATUS, &status);
		if(!status) {
			char info[512];
			glGetProgramInfoLog(program, 512, NULL, info);
			printf("shader error: %s\n", info);
		}

		glDeleteShader(vertex_shader);
		glDeleteShader(fragment_shader);

		float vertices[] = {
			// X, Y, U, V
			-1,  1, 0, 0,
			-1, -1, 0, 1,
			 1,  1, 1, 0,
			 1, -1, 1, 1,
		};

		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), vertices, GL_STATIC_DRAW);

		// load data
		glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), (GLvoid*)0);
		glEnableVertexAttribArray(0);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	video_state v_state;
	v_state.frame_data = (uint8_t*)malloc(video_track->width * video_track->height * 3);

	std::thread th([&v_state, mp4, video_track]() {
		if(video_track->bs_type != H265_HEVC) {
			// initialize codec
			avcodec_register(&ff_h264_decoder);
			AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
			AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
			avcodec_open2(codec_ctx, codec, NULL);
			AVFrame *frame = av_frame_alloc();
			AVPacket packet;
			{
				mp4_sample sample;
				int got_frame 		=  0;
				int frame_decode 	= -1;
				int step = 0; // 0: feed, 1: decoding, 2: wait

				printf("decoding loop ...\n");
				// decoding loop

				while(frame_decode < video_track->sample_count && !v_state.kill) {
					// texture updated queue next frame
					if(step != 1 &&
						!v_state.load_new_frame &&
						frame_decode < v_state.video_frame) {
						frame_decode++;
						step = 0;
					}
					if(step == 0) {
						mp4_read_video_sample(mp4, video_track, frame_decode, sample, true);
						av_init_packet(&packet);
						packet.data = sample.data;
						packet.size = sample.size;
						step++;
					} else if(step == 1) {
						avcodec_decode_video2(codec_ctx, frame, &got_frame, &packet);
						if(got_frame) {
							yuv420rgb_strided(frame->data[0], frame->data[1], frame->data[2], frame->linesize[0], frame->linesize[1], v_state.frame_data, video_track->width, video_track->height);
							v_state.load_new_frame = true;
							step = 2;
							mp4_free_sample(sample);
						}
					}
				}
			}
			printf("thread decoder free\n");
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
			de265_set_verbosity(0);
			{
				mp4_sample sample;
				int got_frame 		=  0;
				int frame_decode 	= 0;
				int step = 0; // 0: feed, 1: decoding, 2: wait

				int pos = 0;
				int more = 1;

				printf("decoding loop ...\n");
				// decoding loop

				while(frame_decode < video_track->sample_count && !v_state.kill) {
					// texture updated queue next frame
					if(step != 1 &&
						!v_state.load_new_frame &&
						frame_decode < v_state.video_frame) {
						step = 0;
					}
					if(step == 0) {
						mp4_read_video_sample(mp4, video_track, frame_decode, sample, true);
						err = de265_push_data(ctx, sample.data, sample.size, pos, (void *)2);
						pos += sample.size;
						frame_decode++;
						if (err != DE265_OK) {
							break;
						}
						step++;
					} else if(step == 1) {
						more = 1;
						while(more) {
							more = 0;
							err = de265_decode(ctx, &more);
							if (err != DE265_OK) {
								break;
							}
							const de265_image *img = de265_get_next_picture(ctx);
							if (img) {
								int stride, stride_uv;
									const uint8_t* plane_y = de265_get_image_plane(img, 0, &stride);
									const uint8_t* plane_u = de265_get_image_plane(img, 1, &stride_uv);
									yuv420rgb_strided(plane_y, plane_u, de265_get_image_plane(img, 2, &stride_uv), stride, stride_uv, v_state.frame_data, video_track->width, video_track->height);
								step = 2;
								v_state.load_new_frame = true;
							} else {
								// more data
								step = 0;
							}
						}
					}
				}

				de265_flush_data(ctx);
			}
		}
	});

    float accumTime = 0.0f;
    uint32_t accumFrames = 0;

	float video_time = 0;
	float segs_per_frame = 1.0f / video_track->fps;
	char windowName[64];

#ifdef MR_USE_AUDIO_PLAYER
    std::thread audio_th;
    if (have_audio) {
        audio_th = std::thread([filepath, &audio_state]() {
            mp4_file* audio_mp4 = mp4_open(filepath, false);
            mp4_track* track = mp4_get_track(audio_mp4, AUDIO_TRACK);
            HANDLE_AACDECODER decoder = aacDecoder_Open(TT_MP4_RAW, 1);
            bool configured = decoder && track && !track->audio_specific_config.empty();
            if (configured) {
                UCHAR* asc = track->audio_specific_config.data();
                UINT size = static_cast<UINT>(track->audio_specific_config.size());
                configured = aacDecoder_ConfigRaw(decoder, &asc, &size) == AAC_DEC_OK;
            }
            if (!configured) fprintf(stderr, "audio: failed to configure AAC decoder\n");
            INT_PCM pcm[PCM_BUFFER_SIZE];
            for (int index = 0; configured && index < track->sample_count; ++index) {
                {
                    std::lock_guard<std::mutex> lock(audio_state.mutex);
                    if (audio_state.stop) break;
                }
                mp4_sample sample;
                mp4_read_audio_sample(audio_mp4, track, index, sample);
                UCHAR* input = sample.data;
                UINT size = static_cast<UINT>(sample.size);
                UINT valid = size;
                AAC_DECODER_ERROR result = aacDecoder_Fill(decoder, &input, &size, &valid);
                free(sample.data);
                if (result != AAC_DEC_OK || valid != 0) continue;
                if (aacDecoder_DecodeFrame(decoder, pcm, PCM_BUFFER_SIZE, 0) != AAC_DEC_OK) continue;
                CStreamInfo* info = aacDecoder_GetStreamInfo(decoder);
                if (!info || info->sampleRate <= 0 || info->numChannels <= 0) continue;
                if (!audio_state.device_initialized) {
                    audio_state.channels = info->numChannels;
                    audio_state.sample_rate = info->sampleRate;
                    ma_device_config config = ma_device_config_init(ma_device_type_playback);
                    config.playback.format = ma_format_s16;
                    config.playback.channels = audio_state.channels;
                    config.sampleRate = audio_state.sample_rate;
                    config.dataCallback = audio_stream_callback;
                    config.pUserData = &audio_state;
                    if (ma_device_init(NULL, &config, &audio_state.device) != MA_SUCCESS) {
                        fprintf(stderr, "audio: failed to initialize device\n");
                        break;
                    }
                    audio_state.device_initialized = true;
                }
                if (info->numChannels != static_cast<int>(audio_state.channels) ||
                    info->sampleRate != static_cast<int>(audio_state.sample_rate)) {
                    fprintf(stderr, "audio: AAC format changed during playback\n");
                    break;
                }
                std::unique_lock<std::mutex> lock(audio_state.mutex);
                const size_t max_samples = static_cast<size_t>(audio_state.sample_rate) * audio_state.channels;
                audio_state.changed.wait(lock, [&]() { return audio_state.stop || audio_state.samples.size() < max_samples; });
                if (audio_state.stop) break;
                audio_state.samples.insert(audio_state.samples.end(), pcm, pcm + info->frameSize * info->numChannels);
                if (!audio_state.ready && audio_state.samples.size() >= max_samples / 4) {
                    audio_state.ready = true;
                    audio_state.changed.notify_all();
                }
            }
            if (decoder) aacDecoder_Close(decoder);
            fclose(audio_mp4->fs->file);
            delete[] audio_mp4->fs->temp_buffer;
            delete audio_mp4->fs;
            delete audio_mp4;
            {
                std::lock_guard<std::mutex> lock(audio_state.mutex);
                audio_state.ready = true;
            }
            audio_state.changed.notify_all();
        });
    }
#endif

    // Decode the first video frame and prebuffer audio concurrently.
    v_state.wait();
#ifdef MR_USE_AUDIO_PLAYER
    if (have_audio) {
        std::unique_lock<std::mutex> lock(audio_state.mutex);
        audio_state.changed.wait(lock, [&]() { return audio_state.ready; });
        bool start_audio = audio_state.device_initialized && !audio_state.samples.empty();
        lock.unlock();
        if (start_audio && ma_device_start(&audio_state.device) != MA_SUCCESS)
            fprintf(stderr, "audio: failed to start device\n");
    }
#endif

	// Reset the clock so the first dt does not include the audio wait time
	lastTime = (float)glfwGetTime();
	video_time = 0;

    while(!glfwWindowShouldClose(window) && v_state.video_frame < video_track->sample_count)
    {
		glViewport(0, 0, window_width, window_height);
        float curTime = (float)glfwGetTime();
        float dt = curTime - lastTime;
        lastTime = curTime;

        accumTime += dt;
        accumFrames++;

        if(accumTime >= 1.0f)
        {
            float avgDt = accumTime / accumFrames;
            snprintf(windowName, sizeof(windowName), "MP4 player [FPS: %.0f (%.2fms)]", 1.0f / avgDt, avgDt * 1000.0f);
            glfwSetWindowTitle(window, windowName);
            accumTime -= 1.0f;
            accumFrames = 0;
        }

        {
			if(v_state.load_new_frame) {
				if(tex == -1) {
					glGenTextures(1, &tex);
					glBindTexture(GL_TEXTURE_2D, tex);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, video_track->width, video_track->height, 0, GL_RGB, GL_UNSIGNED_BYTE, v_state.frame_data);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				} else {
					glBindTexture(GL_TEXTURE_2D, tex);
					glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, video_track->width, video_track->height, GL_RGB, GL_UNSIGNED_BYTE, v_state.frame_data);
				}
				v_state.load_new_frame = false;
			}

			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			glUseProgram(program);
			glActiveTexture(GL_TEXTURE0);
        	glBindTexture(GL_TEXTURE_2D, tex);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}

		v_state.video_frame = (int)(video_time / segs_per_frame);
		video_time += dt;

		printf("\r %d:%s%d ", (int)(video_time/60.0f), (int)(video_time) % 60 < 10 ? "0": "", (int)(video_time) % 60);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

	v_state.kill = true;

    glfwTerminate();

	th.join();
#ifdef MR_USE_AUDIO_PLAYER
    {
        std::lock_guard<std::mutex> lock(audio_state.mutex);
        audio_state.stop = true;
    }
    audio_state.changed.notify_all();
    if (audio_th.joinable()) audio_th.join();
    if (audio_state.device_initialized) ma_device_uninit(&audio_state.device);
#endif
#else
	printf("mp4 player not available, if you have glfw, just compile with -DMR_OGL_PLAYER=ON");
#endif
}

void mp3_play(const char* path) {
#ifdef MR_USE_AUDIO_PLAYER
    ma_engine engine;
    if (ma_engine_init(NULL, &engine) != MA_SUCCESS) {
        fprintf(stderr, "audio: failed to initialize device\n");
        return;
    }
    ma_sound sound;
    if (ma_sound_init_from_file(&engine, path, 0, NULL, NULL, &sound) != MA_SUCCESS) {
        fprintf(stderr, "audio: failed to open %s\n", path);
        ma_engine_uninit(&engine);
        return;
    }
    if (ma_sound_start(&sound) == MA_SUCCESS) {
        while (ma_sound_is_playing(&sound)) {
            float time = 0;
            ma_sound_get_cursor_in_seconds(&sound, &time);
            printf("\r %d:%02d ", (int)(time / 60), (int)time % 60);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    ma_sound_uninit(&sound);
    ma_engine_uninit(&engine);
#else
    printf("mp3 player not available; compile with -DMR_AUDIO_PLAYER=ON\n");
#endif
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
		int path_len = strlen(argv[1]);
		if(path_len > 4 && strcmp(argv[1] + path_len - 4, ".mp4") == 0) {
			mp4_file* mp4 = mp4_open(argv[1], false);
			printf("MP4 file information\n");
			print_mp4_info(mp4);
			if(argc >= 3) {
				std::string op = std::string(argv[2]);
				if((op == "-d" || op == "-c") && argc >= 4) {
					decode_and_save(mp4, argc, argv, op == "-c");
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
				}  else if(op == "-ea" && argc == 3) {
					mp4_track* audio_track = mp4_get_track(mp4, AUDIO_TRACK);
					if(!audio_track) {
						fprintf(stderr, "missing audio track");
						return 0;
					}
					mp4_sample smpl;
					audio_data aud;
					std::vector<short> data;
					// MP4 audio is raw AAC: use TT_MP4_RAW with AudioSpecificConfig.
					HANDLE_AACDECODER aac_dec = aacDecoder_Open(TT_MP4_RAW, 1);
					if (!aac_dec) {
						fprintf(stderr, "aacDecoder_Open failed\n");
						return 0;
					}
					if (!audio_track->audio_specific_config.empty()) {
						UCHAR* asc = const_cast<UCHAR*>(audio_track->audio_specific_config.data());
						UINT asc_size = static_cast<UINT>(audio_track->audio_specific_config.size());
						if (aacDecoder_ConfigRaw(aac_dec, &asc, &asc_size) != AAC_DEC_OK) {
							fprintf(stderr, "aacDecoder_ConfigRaw failed\n");
						}
					}
					INT_PCM pcm_out[PCM_BUFFER_SIZE];
					for(int s = 0; s < audio_track->sample_count; s++) {
						mp4_read_audio_sample(mp4, audio_track, s, smpl);
						// Feed the decoder the full sample.
						size_t bytes_fed = 0;
						while (bytes_fed < smpl.size) {
							UCHAR *in_ptr = smpl.data + bytes_fed;
							UINT bytes_valid = static_cast<UINT>(smpl.size - bytes_fed);
							if (aacDecoder_Fill(aac_dec, &in_ptr, &bytes_valid, &bytes_valid) != AAC_DEC_OK) {
								fprintf(stderr, "aacDecoder_Fill error (sample %d)\n", s);
								free(smpl.data);
								goto audio_done;
							}
							bytes_fed = smpl.size - bytes_valid;
						}
						AAC_DECODER_ERROR err = aacDecoder_DecodeFrame(aac_dec, pcm_out, PCM_BUFFER_SIZE, 0);
						if (err != AAC_DEC_OK) {
							fprintf(stderr, "Decode error: %x (sample %d)\n", err, s);
							free(smpl.data);
							continue;
						}

						CStreamInfo *info = aacDecoder_GetStreamInfo(aac_dec);
						if (!info || info->sampleRate <= 0) {
							fprintf(stderr, "No stream info\n");
							break;
						}

						for(int i = 0; i < (info->frameSize * info->numChannels); i ++) {
							data.push_back(pcm_out[i]);
						}
					}
					audio_done:;
					aud.data = data.data();
					aud.data_size = data.size() * 2;
					aud.samplerate = audio_track->sample_rate;
					aud.stereo = audio_track->num_channels == 2;
					wav_write(&aud);
					printf("acc data extracted");
				} else if(op == "-p" && argc == 3) {
					mp4_play(mp4, argv[1]);
				} else {
					print_usage();
				}
			}
		} else if(path_len > 4 && strcmp(argv[1] + path_len - 4, ".mp3") == 0) {
			if(argc >= 3) {
				std::string op = std::string(argv[2]);
				if(op == "-p" && argc == 3) {
					mp3_play(argv[1]);
				} else if(op == "-w" && argc == 3) {
					audio_data* aud = mp3_open(argv[1]);
                        if (aud) { wav_write(aud); free(aud->data); delete aud; }
                        else fprintf(stderr, "Could not decode %s\n", argv[1]);
				} else {
					print_usage();
				}
			}
		}
	} else {
		print_usage();
	}
	return 1;
}