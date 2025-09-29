# Media Reader

You can parse mp4 videos (h.264) and get the frames in a rgb buffer and parse mp3 and get the pcm raw audio data

# External Libraries

 - h264: decoder part from [ffmpeg](https://github.com/FFmpeg/FFmpeg)
 - [libdec265](https://github.com/strukturag/libde265)
 - stb_image writer
  - [FDK AAC Decoder](https://github.com/mstorsjo/fdk-aac)

# Limitations

 - gcc 13.2 max, gcc 14.2 fails with w32threads

 - Just h.264 and h.265 videos are supported
