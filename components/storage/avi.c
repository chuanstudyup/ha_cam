
/* 
Generate AVI format for recorded videos

s60sc 2020, 2022
*/

/* AVI file format:
header:
 310 uint8_ts
per jpeg:
 4 uint8_t 00dc marker
 4 uint8_t jpeg size
 jpeg frame content
0-3 uint8_ts filler to align on DWORD boundary
per PCM (audio file)
 4 uint8_t 01wb marker
 4 uint8_t pcm size
 pcm content
 0-3 uint8_ts filler to align on DWORD boundary
footer:
 4 uint8_t idx1 marker
 4 uint8_t index size
 per jpeg:
  4 uint8_t 00dc marker
  4 uint8_t 0000
  4 uint8_t jpeg location
  4 uint8_t jpeg size
 per pcm:
  4 uint8_t 01wb marker
  4 uint8_t 0000
  4 uint8_t pcm location
  4 uint8_t pcm size

  //https://blog.csdn.net/weixin_47852035/article/details/121577751
*/
#include "avi.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "cam_info.h"
#include "esp_heap_caps.h"

#define WAVTEMP "/current.wav"

#define ps_malloc(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM)

#define IDX_ENTRY 16 // uint8_ts per index entry

// avi header data

const uint8_t wbBuf[4] = {0x30, 0x31, 0x77, 0x62};   // 01wb
static const uint8_t idx1Buf[4] = {0x69, 0x64, 0x78, 0x31}; // idx1
static const uint8_t zeroBuf[4] = {0x00, 0x00, 0x00, 0x00}; // 0000
static uint8_t* idxBuf[2] = {NULL, NULL};
const uint8_t dcBuf[4] = {0x30, 0x30, 0x64, 0x63};

uint8_t aviHeader[AVI_HEADER_LEN] = { // AVI header template
  0x52, 0x49, 0x46, 0x46, //"RIFF"
  0x00, 0x00, 0x00, 0x00, //size
  0x41, 0x56, 0x49, 0x20, //"AVI" 0x20 is space

  0x4C, 0x49, 0x53, 0x54, //"LIST"
  0x16, 0x01, 0x00, 0x00, //size
  0x68, 0x64, 0x72, 0x6C, //"hdrl"
  0x61, 0x76, 0x69, 0x68, //"avih"
  0x38, 0x00, 0x00, 0x00, //size
  0x00, 0x00, 0x00, 0x00, //MicrosePerFrame 显示每帧所需的时间ns，定义avi的显示速率
  0x00, 0x00, 0x00, 0x00, //MaxBytesPerSec 最大数据传输率
  0x00, 0x00, 0x00, 0x00, //PaddingGranularity;   //记录块的长度须为此值的倍数，通常是2048
  0x10, 0x00, 0x00, 0x00, //Flags;       // AVI文件的特殊属性，包含文件中的任何标志字。如：有无索引块，是否是interlaced，是否含版权信息等
  0x00, 0x00, 0x00, 0x00, //TotalFrames;  	    // 数据帧的总数
  0x00, 0x00, 0x00, 0x00, //InitialFrames;     // 在开始播放前需要的帧数
  0x01, 0x00, 0x00, 0x00, //Streams;           //文件中包含的数据流种类
  0x00, 0x00, 0x00, 0x00, //SuggestedBufferSize;//建议使用的缓冲区的大小，通常为存储一帧图像以及同步声音所需要的数据之和，大于最大的CHUNK的大小
  0x00, 0x00, 0x00, 0x00, //Width;             //图像宽，像素
  0xe0, 0x00, 0x00, 0x00, //Height;            //图像高，像素
  0x00, 0x00, 0x00, 0x00, //Reserved
  0x00, 0x00, 0x00, 0x00, //Reserved
  0x00, 0x00, 0x00, 0x00, //Reserved
  0x00, 0x00, 0x00, 0x00, //Reserved

  0x4C, 0x49, 0x53, 0x54, //"LIST" 一个strl list中至少包含一个strh块和一个strf块。文件中有多少个流，就对应有多少个strl list。
  0x6C, 0x00, 0x00, 0x00, //size
  0x73, 0x74, 0x72, 0x6C, //"strl"
  0x73, 0x74, 0x72, 0x68, //"strh"
  0x30, 0x00, 0x00, 0x00, //size
  0x76, 0x69, 0x64, 0x73, //fccType "vids" // 流的类型: auds(音频流) vids(视频流) mids(MIDI流) txts(文字流)
  0x4D, 0x4A, 0x50, 0x47, //fccHandler "MJPG"// 指定流的处理者，对于音视频来说就是解码器
  0x00, 0x00, 0x00, 0x00, //Flags;              // 标记：是否允许这个流输出？调色板是否变化？
  0x00, 0x00, 0x00, 0x00, //Priority;             // 流的优先级（当有多个相同类型的流时优先级最高的为默认流）
  0x00, 0x00, 0x00, 0x00, //Language;             // 语言
  0x01, 0x00, 0x00, 0x00, //InitialFrames;      // 为交互格式指定初始帧数
  0x00, 0x00, 0x00, 0x00, //Scale;              // 每帧视频大小或者音频采样大小
  0x00, 0x00, 0x00, 0x00, //Rate;               // dwScale/dwRate，每秒采样率
  0x0A, 0x00, 0x00, 0x00, //Start;              // 流的开始时间
  0x00, 0x00, 0x00, 0x00, //Length;             // 流的长度（单位与dwScale和dwRate的定义有关）
  0x00, 0x00, 0x00, 0x00, //SuggestedBufferSize;// 读取这个流数据建议使用的缓存大小
  0x00, 0x00, 0x00, 0x00, //Quality;            // 流数据的质量指标（0 ~ 10,000）
  0x73, 0x74, 0x72, 0x66, //"strf"
  0x28, 0x00, 0x00, 0x00, //size
  0x28, 0x00, 0x00, 0x00, //size
  0x00, 0x00, 0x00, 0x00, //Width;
  0x00, 0x00, 0x00, 0x00, //Height;
  0x01, 0x00, //Planes;
  0x18, 0x00, //BitCount;
  0x4D, 0x4A, 0x50, 0x47, //Compression; "MJPG"
  0x00, 0x00, 0x00, 0x00, //SizeImage;
  0x00, 0x00, 0x00, 0x00, //XPelsPerMeter;
  0x00, 0x00, 0x00, 0x00, //YPelsPerMeter;
  0x00, 0x00, 0x00, 0x00, //ClrUsed;
  0x00, 0x00, 0x00, 0x00, //ClrImportant;
  
  0x4C, 0x49, 0x53, 0x54, //"LIST"
  0x56, 0x00, 0x00, 0x00, //size
  0x73, 0x74, 0x72, 0x6C, //"strl"
  0x73, 0x74, 0x72, 0x68, //"strh"
  0x30, 0x00, 0x00, 0x00, //size
  0x61, 0x75, 0x64, 0x73, //fccType "auds"
  0x00, 0x00, 0x00, 0x00, //fccHandler
  0x00, 0x00, 0x00, 0x00, //Flags;
  0x00, 0x00, 0x00, 0x00, //Priority;
  0x00, 0x00, 0x00, 0x00, //Language;
  0x01, 0x00, 0x00, 0x00, //InitialFrames;
  0x11, 0x2B, 0x00, 0x00, //Scale;  
  0x00, 0x00, 0x00, 0x00, //Rate;    
  0x00, 0x00, 0x00, 0x00, //Start;  
  0x11, 0x2B, 0x00, 0x00, //Length;  
  0x00, 0x00, 0x00, 0x00, //SuggestedBufferSize;
  0x02, 0x00, 0x00, 0x00, //Quality;
  0x73, 0x74, 0x72, 0x66, //"strf"
  0x12, 0x00, 0x00, 0x00, //size
  0x01, 0x00, //FormatTag;
  0x01, 0x00, //Channels;               // 声道数
  0x11, 0x2B, 0x00, 0x00, //SamplesPerSec;         // 采样率
  0x11, 0x2B, 0x00, 0x00, //AvgBytesPerSec;        // 每秒的数据量
  0x02, 0x00, //BlockAlign;             // 数据块对齐标志
  0x10, 0x00, //BitsPerSample;          // 每次采样的数据量
  0x00, 0x00, //Size;                  // 大小
  
  0x4C, 0x49, 0x53, 0x54, //"LIST"
  0x00, 0x00, 0x00, 0x00, //size of movi
  0x6D, 0x6F, 0x76, 0x69, //"movi"
};

// separate index for motion capture and timelapse
static uint32_t idxPtr[2];
static uint32_t idxOffset[2];
static uint32_t moviSize[2];
static uint32_t audSize;
static uint32_t indexLen[2];
static FILE* wavFile;
bool haveSoundFile = false;

void prepAviIndex(bool isTL) {
  // prep buffer to store index data, gets appended to end of file
  if (idxBuf[isTL] == NULL) idxBuf[isTL] = (uint8_t*)ps_malloc((MAXFRAMES+1)*IDX_ENTRY); // include some space for audio index
  memcpy(idxBuf[isTL], idx1Buf, 4); // index header
  idxPtr[isTL] = CHUNK_HDR;  // leave 4 uint8_ts for index size
  moviSize[isTL] = indexLen[isTL] = 0;
  idxOffset[isTL] = 4; // 4 uint8_t offset
}

void buildAviHdr(uint8_t FPS, uint8_t frameType, uint16_t frameCnt, bool isTL) {
  // update AVI header template with file specific details
  uint32_t aviSize = moviSize[isTL] + AVI_HEADER_LEN + ((CHUNK_HDR+IDX_ENTRY) * (frameCnt+(haveSoundFile?1:0))); // AVI content size 
  // update aviHeader with relevant stats
  memcpy(aviHeader+4, &aviSize, 4);
  uint32_t usecs = (uint32_t)round(1000000.0f / FPS); // usecs_per_frame 
  memcpy(aviHeader+0x20, &usecs, 4); 
  memcpy(aviHeader+0x30, &frameCnt, 2);
  memcpy(aviHeader+0x8C, &frameCnt, 2);
  memcpy(aviHeader+0x84, &FPS, 1);
  uint32_t dataSize = moviSize[isTL] + ((frameCnt+(haveSoundFile?1:0)) * CHUNK_HDR) + 4; 
  memcpy(aviHeader+0x12E, &dataSize, 4); // data size 

  // apply video framesize to avi header
  uint8_t words[2];
  words[0] = frameData[frameType].frameWidth & 0xFF;
  words[1] = frameData[frameType].frameWidth >> 8;
  memcpy(aviHeader+0x40, words, 2);
  memcpy(aviHeader+0xA8, words, 2);
  words[0] = frameData[frameType].frameHeight & 0xFF;
  words[1] = frameData[frameType].frameHeight >> 8;
  memcpy(aviHeader+0x44, words, 2);
  memcpy(aviHeader+0xAC, words, 2);

#if INCLUDE_AUDIO
  uint8_t withAudio = 2; // increase number of streams for audio
  if (isTL) memcpy(aviHeader+0x100, zeroBuf, 4); // no audio for timelapse
  else {
    if (haveSoundFile) memcpy(aviHeader+0x38, &withAudio, 1); 
    memcpy(aviHeader+0x100, &audSize, 4); // audio data size
  }
  // apply audio details to avi header
  memcpy(aviHeader+0xF8, &SAMPLE_RATE, 4);
  uint32_t uint8_tsPerSec = SAMPLE_RATE * 2;
  memcpy(aviHeader+0x104, &uint8_tsPerSec, 4); // suggested buffer size
  memcpy(aviHeader+0x11C, &SAMPLE_RATE, 4);
  memcpy(aviHeader+0x120, &uint8_tsPerSec, 4); // uint8_ts per sec
#else
  memcpy(aviHeader+0x100, zeroBuf, 4);
#endif

  // reset state for next recording
  moviSize[isTL] = idxPtr[isTL] = 0;
  idxOffset[isTL] = 4; // 4 uint8_t offset
}

void buildAviIdx(uint32_t dataSize, bool isVid, bool isTL) {
  // build AVI video index into buffer - 16 uint8_ts per frame
  // called from saveFrame() for each frame
  moviSize[isTL] += dataSize;
  if (isVid) memcpy(idxBuf[isTL]+idxPtr[isTL], dcBuf, 4);
  else memcpy(idxBuf[isTL]+idxPtr[isTL], wbBuf, 4);
  
  memcpy(idxBuf[isTL]+idxPtr[isTL]+4, zeroBuf, 4);
  memcpy(idxBuf[isTL]+idxPtr[isTL]+8, &idxOffset[isTL], 4); 
  memcpy(idxBuf[isTL]+idxPtr[isTL]+12, &dataSize, 4); 
  idxOffset[isTL] += dataSize + CHUNK_HDR;
  idxPtr[isTL] += IDX_ENTRY; 
}

uint32_t writeAviIndex(uint8_t* clientBuf, uint32_t buffSize, bool isTL) {
  // write completed index to avi file
  // called repeatedly from closeAvi() until return 0
  if (idxPtr[isTL] < indexLen[isTL]) {
    if (indexLen[isTL]-idxPtr[isTL] > buffSize) {
      memcpy(clientBuf, idxBuf[isTL]+idxPtr[isTL], buffSize);
      idxPtr[isTL] += buffSize;
      return buffSize;
    } else {
      // final part of index
      uint32_t final = indexLen[isTL]-idxPtr[isTL];
      memcpy(clientBuf, idxBuf[isTL]+idxPtr[isTL], final);
      idxPtr[isTL] = indexLen[isTL];
      return final;    
    }
  }
  return idxPtr[isTL] = 0;
}
  
void finalizeAviIndex(uint16_t frameCnt, bool isTL) {
  // update index with size
  uint32_t sizeOfIndex = (frameCnt+(haveSoundFile?1:0))*IDX_ENTRY;
  memcpy(idxBuf[isTL]+4, &sizeOfIndex, 4); // size of index 
  indexLen[isTL] = sizeOfIndex + CHUNK_HDR;
  idxPtr[isTL] = 0; // pointer to index buffer
}

bool haveWavFile(bool isTL) {
  haveSoundFile = false;
  audSize = 0;
#if INCLUDE_AUDIO
  if (isTL) return false;
  // check if wave file exists
  if (0 != access(WAVTEMP, F_OK)) return false; 
  // open it and get its size
  wavFile = fopen(WAVTEMP, "rb");
  if (wavFile) {
    // add sound file index
    fseek(wavFile, 0, SEEK_END); // get file size
    audSize = ftell(wavFile) - WAV_HDR_LEN; // minus header length
    buildAviIdx(audSize, false); 
    // add sound file header
    fseek(wavFile, WAV_HDR_LEN, SEEK_SET); // skip over header
    haveSoundFile = true;
  } 
#endif
  return haveSoundFile;
}

uint32_t writeWavFile(uint8_t* clientBuf, uint32_t buffSize) {
  // read in wav file and write to avi file
  // called repeatedly from closeAvi() until return 0
  static uint32_t offsetWav = CHUNK_HDR;
  if (offsetWav) {
    // add sound file header         
    memcpy(clientBuf, wbBuf, 4);     
    memcpy(clientBuf+4, &audSize, 4); 
  }
  uint32_t readLen = fread(clientBuf+offsetWav, 1, buffSize-offsetWav, wavFile) + offsetWav; 
  offsetWav = 0;
  if (readLen) return readLen; 
  // get here if finished
  fclose(wavFile);
  remove(WAVTEMP); // delete wav file
  offsetWav = CHUNK_HDR;
  return 0;
}
