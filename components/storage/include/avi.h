#ifndef _AVI_H_
#define	_AVI_H_

#include <stdint.h>
#include <stdbool.h>

#define AVI_HEADER_LEN 310 // AVI header length
#define CHUNK_HDR 8 // uint8_ts per jpeg hdr in AVI 
#define MAXFRAMES 20000 // maximum number of frames in video before auto close

extern const uint8_t dcBuf[];   // 00dc

void buildAviHdr(uint8_t FPS, uint8_t frameType, uint16_t frameCnt, bool isTL);
void buildAviIdx(uint32_t dataSize, bool isVid, bool isTL);
void prepAviIndex(bool isTL);
uint32_t writeAviIndex(uint8_t* clientBuf, uint32_t buffSize, bool isTL);
void finalizeAviIndex(uint16_t frameCnt, bool isTL);
bool haveWavFile(bool isTL);
uint32_t writeWavFile(uint8_t* clientBuf, uint32_t buffSize);

#endif