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

#ifdef MR_USE_OAL_PLAYER
#include <AL/al.h>
#include <AL/alc.h>
#endif

#if defined(MR_USE_OAL_PLAYER) || defined(MR_USE_OGL_PLAYER)
#include <thread>
#endif

// libde265
#include "de265.h"

static void wav_write(audio_data* aud) {
	FILE* wav = fopen("output.wav", "wb");
	uint8_t* wav_header = new uint8_t[44];
	memcpy(wav_header, "RIFF\xff\xff\xff\xffWAVEfmt ", 16);
	uint16_t channels_ = (aud->stereo ? 2 : 1);
	int file_size_ = 44 + aud->data_size;
	memcpy(wav_header + 4, &file_size_, 4);
	int tmp = 16; // size
	memcpy(wav_header + 16, &tmp, 4);
	short tmp2 = 1; // PCM
	memcpy(wav_header + 20, &tmp2, 2);
	memcpy(wav_header + 22, &channels_, 2);
	tmp = aud->samplerate; // sample rate
	memcpy(wav_header + 24, &tmp, 4);
	tmp = aud->samplerate * channels_ * 2; // byte rate
	memcpy(wav_header + 28, &tmp, 4);
	tmp2 = 4; // align
	memcpy(wav_header + 32, &tmp2, 2);
	tmp2 = 16; // bits per sample
	memcpy(wav_header + 34, &tmp2, 2);
	memcpy(wav_header + 36, "data\xff\xff\xff\xff", 8);
	memcpy(wav_header + 40, &aud->data_size, 4);
	fwrite(wav_header, 1, 44, wav);
	fwrite(aud->data, 1, aud->data_size, wav);
	fclose(wav);
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
					yuv420rgb_multi_buffer(frame->data[0], frame->data[1], frame->data[2], rgb_buffer, frame->width, frame->height);
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
							int stride;
							yuv420rgb_multi_buffer(
								de265_get_image_plane(img, 0, &stride),
								de265_get_image_plane(img, 1, &stride),
								de265_get_image_plane(img, 2, &stride), rgb_buffer, video_track->width, video_track->height);
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
	printf("	-p : play mp4 as an opengl texture (only h.264) / mp3 with openal\n");
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
	int video_frame = 0;
	bool load_new_frame = false;
	uint8_t* frame_data = NULL;
	bool kill = false;

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

void mp4_play(mp4_file* mp4) {
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

#ifdef MR_USE_OAL_PLAYER
	ALCdevice* dev = alcOpenDevice(NULL);
	if(!dev) {
		printf("audio: failed to open device");
		return;
	}
	ALCcontext *ctx = alcCreateContext(dev, NULL);
	if(!alcMakeContextCurrent(ctx)) {
		printf("audio: failed to make context");
		alcCloseDevice(dev);
		return;
	}
	mp4_sample smpl;
	audio_data aud;
	std::vector<short> audio_data;
	
	{
		mp4_track* audio_track = mp4_get_track(mp4, AUDIO_TRACK);
		if(!audio_track) {
			fprintf(stderr, "missing audio track");
			return;
		}
		HANDLE_AACDECODER aac_dec = aacDecoder_Open(TT_MP4_ADTS, 1);
		INT_PCM pcm_out[PCM_BUFFER_SIZE];
		for(int s = 0; s < audio_track->sample_count; s++) {
			mp4_read_audio_sample(mp4, audio_track, s, smpl);
			UCHAR *in_ptr = smpl.data;
			UINT bytes_valid = smpl.size;
			if (aacDecoder_Fill(aac_dec, &in_ptr, &bytes_valid, &bytes_valid) != AAC_DEC_OK) {
				fprintf(stderr, "aacDecoder_Fill error\n");
				free(smpl.data);
				break;
			}
			AAC_DECODER_ERROR err = aacDecoder_DecodeFrame(aac_dec, pcm_out, PCM_BUFFER_SIZE, 0);
			if (err != AAC_DEC_OK) {
				fprintf(stderr, "Decode error: %x\n", err);
				free(smpl.data);
				continue;
			}

			CStreamInfo *info = aacDecoder_GetStreamInfo(aac_dec);
			if (!info || info->sampleRate <= 0) {
				fprintf(stderr, "No stream info\n");
				break;
			}

			for(int i = 0; i < (info->frameSize * info->numChannels); i ++) {
				audio_data.push_back(pcm_out[i]);
			}
		}
		aud.samplerate = audio_track->sample_rate;
		aud.stereo = audio_track->num_channels == 2;
	}

	printf("playing in OpenAL\n");

	printf("audio device: %s\n", alGetString(AL_VERSION));

	ALuint buff;
	alGenBuffers(1, &buff);
	alBufferData(buff, aud.stereo ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, audio_data.data(), audio_data.size() * 2, aud.samplerate);

	ALuint src;
	alGenSources(1, &src);
	alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
	alSource3f(src, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
	alSource3f(src, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
	alSourcei(src, AL_BUFFER, buff);
	alSourcePlay(src);
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
							yuv420rgb_multi_buffer(frame->data[0], frame->data[1], frame->data[2], v_state.frame_data, video_track->width, video_track->height);
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
								int stride;
									yuv420rgb_multi_buffer(
										de265_get_image_plane(img, 0, &stride),
										de265_get_image_plane(img, 1, &stride),
										de265_get_image_plane(img, 2, &stride), v_state.frame_data, video_track->width, video_track->height);
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

	v_state.wait();

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
#else
	printf("mp4 player not available, if you have glfw, just compile with -DMR_OGL_PLAYER=ON");
#endif
}

void mp3_play(const char* path) {
#ifdef MR_USE_OAL_PLAYER
	ALCdevice* dev = alcOpenDevice(NULL);
	if(!dev) {
		printf("audio: failed to open device");
		return;
	}
	ALCcontext *ctx = alcCreateContext(dev, NULL);
	if(!alcMakeContextCurrent(ctx)) {
		printf("audio: failed to make context");
		alcCloseDevice(dev);
		return;
	}

	printf("decoding mp3\n");
	audio_data* aud = mp3_open(path);

	printf("playing in OpenAL\n");

	printf("audio device: %s\n", alGetString(AL_VERSION));

	ALuint buff;
	alGenBuffers(1, &buff);
	alBufferData(buff, aud->stereo ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, aud->data, aud->data_size, aud->samplerate);

	ALuint src;
	alGenSources(1, &src);
	alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
	alSource3f(src, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
	alSource3f(src, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
	alSourcei(src, AL_BUFFER, buff);
	alSourcePlay(src);

	ALint state;
	do {
		alGetSourcei(src, AL_SOURCE_STATE, &state);
		float time;
		alGetSourcef(src, AL_SEC_OFFSET, &time);
		printf("\r %d:%s%d ", (int)(time/60.0f), (int)(time) % 60 < 10 ? "0": "", (int)(time) % 60);
		std::this_thread::sleep_for(std::chrono::milliseconds(900));
	} while(state == AL_PLAYING);

	alDeleteSources(1, &src);
	alDeleteBuffers(1, &buff);
	alcMakeContextCurrent(NULL);
	alcDestroyContext(ctx);
	alcCloseDevice(dev);

	free(aud->data);
#else
	printf("mp3 player not available, if you have openal, just compile with -DMR_OAL_PLAYER=ON");
#endif
}

int main(int argc, char* argv[]) {
	//mp3_open("D:\\proyectos\\OpenMP3-master\\build\\bin\\Release\\test.mp3");
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
					HANDLE_AACDECODER aac_dec = aacDecoder_Open(TT_MP4_ADTS, 1);
					INT_PCM pcm_out[PCM_BUFFER_SIZE];
					for(int s = 0; s < audio_track->sample_count; s++) {
						mp4_read_audio_sample(mp4, audio_track, s, smpl);
						UCHAR *in_ptr = smpl.data;
						UINT bytes_valid = smpl.size;
						if (aacDecoder_Fill(aac_dec, &in_ptr, &bytes_valid, &bytes_valid) != AAC_DEC_OK) {
							fprintf(stderr, "aacDecoder_Fill error\n");
							free(smpl.data);
							break;
						}
						AAC_DECODER_ERROR err = aacDecoder_DecodeFrame(aac_dec, pcm_out, PCM_BUFFER_SIZE, 0);
						if (err != AAC_DEC_OK) {
							fprintf(stderr, "Decode error: %x\n", err);
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
					aud.data = data.data();
					aud.data_size = data.size() * 2;
					aud.samplerate = audio_track->sample_rate;
					aud.stereo = audio_track->num_channels == 2;
					wav_write(&aud);
					printf("acc data extracted");
				} else if(op == "-p" && argc == 3) {
					mp4_play(mp4);
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
					wav_write(aud);
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