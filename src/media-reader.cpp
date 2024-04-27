#include <map>
#include "media-reader.h"

int clamp(int x, int min, int max) {
    return x > max ? max : (x < min ? min : x);
}

// yuv I420 -> RGB888
void yuv420rgb(uint8_t* yuv, uint8_t* rgb, int width, int height) {
    // yuv I420 layout [y: [width*height], u: [width/2 * height/2], v: [width/2 * height/2]]
    // u, v need upsample (bilinear?)
    int width_half = (width / 2), height_half = (height / 2);

    uint8_t* y_p = yuv;
    uint8_t* u_p = yuv + width * height;
    uint8_t* v_p = u_p + width_half * height_half;

    int uv_x = 1;

    float mat[9] = {
            1.0f, 0.0f, 1.402f,
            1.0f, -0.34414f, -0.71414f,
            1.0f, 1.772f, 0.0f };
    
    int uv_offset = 128;
    int rgb_offset = 0;
    uint8_t* u_p1 = u_p + width_half;
    uint8_t* v_p1 = v_p + width_half;

    for(int y = 0; y < height; y++) {
        bool y0 = (y - 1) % 2 == 0;

        for(int x = 0; x < width; x++) {
            int lum = y_p[y * width + x];
            int u = 0;
            int v = 0;
            bool x0 = (x - 1) % 2 == 0;

            // inplace middle bilinear upscale x4
            if(y == 0 || y == height - 1) {
                if(x == 0) {
                    u = u_p[0];
                    v = v_p[0];
                    uv_x = 1;
                } else {
                    if(x == width - 1) {
                        u = u_p[width_half - 1];
                        v = v_p[width_half - 1];
                    } else if(x0) {
                        u = (u_p[uv_x - 1] * 3 + u_p[uv_x]) / 4;
                        v = (v_p[uv_x - 1] * 3 + v_p[uv_x]) / 4;
                    } else {
                        u = (u_p[uv_x - 1] + u_p[uv_x] * 3) / 4;
                        v = (v_p[uv_x - 1] + v_p[uv_x] * 3) / 4;
                        uv_x ++;
                    }
                }
            } else {
                if(x == 0) {
                    if(y0) {
                        u = (u_p[0] * 3 + u_p1[1]) / 4;
                        v = (v_p[0] * 3 + v_p1[1]) / 4;
                    } else {
                        u = (u_p[0] + u_p1[1] * 3) / 4;
                        v = (v_p[0] + v_p1[1] * 3) / 4;
                    }
                    uv_x = 1;
                } else if(x < width - 1) {
                    if(y0) {
                        if(x0) {
                            u = (u_p[uv_x - 1] * 9 + u_p[uv_x] * 3 + u_p1[uv_x - 1] * 3 + u_p1[uv_x]) / 16;
                            v = (v_p[uv_x - 1] * 9 + v_p[uv_x] * 3 + v_p1[uv_x - 1] * 3 + v_p1[uv_x]) / 16;
                        } else {
                            u = (u_p[uv_x - 1] * 3 + u_p[uv_x] * 9 + u_p1[uv_x - 1] + u_p1[uv_x] * 3) / 16;
                            v = (v_p[uv_x - 1] * 3 + v_p[uv_x] * 9 + v_p1[uv_x - 1] + v_p1[uv_x] * 3) / 16;
                            uv_x ++;
                        }
                    } else {
                        if(x0) {
                            u = (u_p[uv_x - 1] * 3 + u_p[uv_x] + u_p1[uv_x - 1] * 9 + u_p1[uv_x] * 3) / 16;
                            v = (v_p[uv_x - 1] * 3 + v_p[uv_x] + v_p1[uv_x - 1] * 9 + v_p1[uv_x] * 3) / 16;
                        } else {
                            u = (u_p[uv_x - 1] + u_p[uv_x] * 3 + u_p1[uv_x - 1] * 3 + u_p1[uv_x] * 9) / 16;
                            v = (v_p[uv_x - 1] + v_p[uv_x] * 3 + v_p1[uv_x - 1] * 3 + v_p1[uv_x] * 9) / 16;
                            uv_x ++;
                        }
                    }
                } else {
                    int end = width_half - 1;
                    if(y0) {
                        u = (u_p[end] * 3 + u_p1[end]) / 4;
                        v = (v_p[end] * 3 + v_p1[end]) / 4;
                    } else {
                        u = (u_p[end] + u_p1[end] * 3) / 4;
                        v = (v_p[end] + v_p1[end] * 3) / 4;
                        u_p += width_half;
                        v_p += width_half;
                        u_p1 += width_half;
                        v_p1 += width_half;
                    }
                }
            }

            u -= uv_offset;
            v -= uv_offset;

            int r = (int)(mat[0] * lum + mat[1] * u + mat[2] * v);
            int g = (int)(mat[3] * lum + mat[4] * u + mat[5] * v);
            int b = (int)(mat[6] * lum + mat[7] * u + mat[8] * v);

            rgb[rgb_offset] =     clamp(r, 0, 255);
            rgb[rgb_offset + 1] = clamp(g, 0, 255);
            rgb[rgb_offset + 2] = clamp(b, 0, 255);
            rgb_offset += 3;
        }
    }
}