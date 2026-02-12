#ifndef _CAM_INFO_H_
#define _CAM_INFO_H_

#include <stdint.h>

#define MAX_JPEG (524288) // 512KB, UXGA jpeg frame buffer at highest quality 375kB rounded up

typedef struct _frameStruct {
  const char* frameSizeStr;
  const uint16_t frameWidth;
  const uint16_t frameHeight;
  const uint16_t defaultFPS;
  const uint8_t scaleFactor; // (0..4)
  const uint8_t sampleRate; // (1..N)
}frameStruct;

extern const frameStruct frameData[];

#endif