#include "mp4.h"
#include "mp3.h"

void yuv420rgb_multi_buffer(const uint8_t* y_p, const uint8_t* u_p, const uint8_t* v_p, uint8_t* rgb, int width, int height);
void yuv420rgb(uint8_t* yuv, uint8_t* rgb, int width, int height);