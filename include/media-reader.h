#include "mp4.h"
#include "mp3.h"

// planes with a row stride (AVFrame::linesize, de265_get_image_plane): U and V share uv_stride
void yuv420rgb_strided(const uint8_t* y_p, const uint8_t* u_p, const uint8_t* v_p, int y_stride, int uv_stride, uint8_t* rgb, int width, int height);
void yuv420rgb_multi_buffer(const uint8_t* y_p, const uint8_t* u_p, const uint8_t* v_p, uint8_t* rgb, int width, int height);
void yuv420rgb(uint8_t* yuv, uint8_t* rgb, int width, int height);