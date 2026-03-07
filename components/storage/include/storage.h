#ifndef __STORAGE_H__
#define __STORAGE_H__
#include <stdio.h>
#include <stdbool.h>
#include "utilsFS.h"
#include "avi.h"

typedef struct _fnameStruct {
  uint8_t recFPS;
  uint32_t recDuration;
  uint16_t frameCnt;
}fnameStruct;

typedef struct _mjpegStruct {
  size_t buffLen;
  size_t buffOffset;
  size_t jpegSize;
  char* data;
}mjpegStruct;

bool sdcard_init(void);
void sdcard_deinit(void);

/**
 * @brief 打开AVI文件进行写入操作
 * 
 * 根据当前日期和时间生成文件名，并在对应的日期文件夹中创建AVI文件。
 * 函数会创建日期文件夹（如果不存在），打开文件进行二进制写入，
 * 初始化相关计数器和时间统计，并准备AVI文件头和索引。
 * 
 * 注意：文件打开时间会随着SD卡中已有文件数量的增加而增加。
 * 
 * @return true - 文件打开成功并完成初始化
 * @return false - 文件打开失败或文件夹创建失败
 */
bool openAvi();

/**
 * 保存帧数据到SD卡
 * 
 * 将JPEG帧数据写入AVI文件，处理缓冲区管理和帧索引构建
 * 
 * @param frame_buf 帧数据缓冲区指针，包含JPEG编码的图像数据
 * @param len 帧数据长度（字节数）
 * 
 * @note 函数会自动处理4字节对齐，添加AVI帧头，并管理环形缓冲区
 * @note 会记录写入SD卡的时间消耗和总处理时间
 */
void saveFrame(uint8_t* frame_buf, size_t len);

/**
 * @brief 关闭AVI视频文件并完成最终处理
 * 
 * 该函数负责关闭正在录制的AVI文件，包括以下主要功能：
 * 1. 计算视频持续时间
 * 2. 写入剩余的音频数据（如果存在）
 * 3. 生成并写入AVI索引
 * 4. 更新AVI文件头信息
 * 5. 重命名临时文件为最终文件名
 * 6. 记录详细的录制统计信息
 * 7. 处理文件传输和通知（FTP、MQTT、Telegram等）
 * 
 * @return true - 文件成功关闭并满足最小录制时长
 * @return false - 录制时长不足，文件已被删除
 */
bool closeAvi(char *fileName);

/**
 * @brief 获取SD卡的总容量大小
 * 
 * 通过计算SD卡的扇区数量和扇区大小来获取总容量
 * 
 * @return size_t 返回SD卡的总容量大小，单位为字节
 */
size_t getSDCardSize(void);

/**
 * @brief 获取SD卡类型
 *
 * 根据SD卡的属性判断其具体类型，支持以下类型检测：
 * - SDIO类型卡
 * - MMC类型卡  
 * - SDSC (标准容量SD卡)
 * - SDHC (高容量SD卡)
 * - SDHC/SDXC (UHS-I接口的高容量/扩展容量卡)
 *
 * 该函数通过检查SD卡结构体中的标志位和OCR寄存器来判断卡的类型。
 *
 * @return char* 返回SD卡类型的字符串标识符，可能的值包括：
 *               "SDIO", "MMC", "SDSC", "SDHC", "SDHC/SDXC (UHS-I)"
 *               如果无法识别类型，则返回 "Unknown"
 */
char* getCardType();

bool storageInit();

void storageSetFPS(uint8_t fps);

bool openSDfile(const char *streamFile);

fnameStruct* playbackFPS(const char *fname);

mjpegStruct* getNextFrame(bool firstCall, bool *stopPlayback);

void readSD();

#endif // __STORAGE_H__