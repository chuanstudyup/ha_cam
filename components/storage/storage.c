#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_timer.h"
#include "sd_protocol_defs.h"

#include "storage.h"
#include "Camera.h"
#include "ChipInfo.h"
#include "Utils.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))

static const char *TAG = "Storage";

#define SDMMC_PIN_CLK GPIO_NUM_36
#define SDMMC_PIN_CMD GPIO_NUM_35
#define SDMMC_PIN_D0 GPIO_NUM_37

static sdmmc_card_t *card = NULL;

/**
 * @brief Initialize the storage component
 *
 * This function mounts the FAT filesystem on the SD card and initializes the SDMMC host.
 */
bool sdcard_init(void)
{
  esp_err_t ret;
  // Initialize the FAT filesystem
  esp_vfs_fat_mount_config_t mount_config = {
      .format_if_mount_failed = true,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024,
  };

  const char mount_point[] = SD_MOUNT_POINT;
  ESP_LOGI(TAG, "Initializing SD card");

  // Use settings defined above to initialize SD card and mount FAT filesystem.
  // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
  // Please check its source code and implement error recovery when developing
  // production applications.

  ESP_LOGI(TAG, "Using SDMMC peripheral");

  // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
  // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
  // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();

  // This initializes the slot without card detect (CD) and write protect (WP) signals.
  // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;
  slot_config.clk = SDMMC_PIN_CLK;
  slot_config.cmd = SDMMC_PIN_CMD;
  slot_config.d0 = SDMMC_PIN_D0;

  // Enable internal pullups on enabled pins. The internal pullups
  // are insufficient however, please make sure 10k external pullups are
  // connected on the bus. This is for debug / example purpose only.
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  ESP_LOGI(TAG, "Mounting filesystem");
  ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

  if (ret != ESP_OK)
  {
    if (ret == ESP_FAIL)
    {
      ESP_LOGE(TAG, "Failed to mount filesystem.");
    }
    else
    {
      ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                    "Make sure SD card lines have pull-up resistors in place.",
               esp_err_to_name(ret));
    }
    return false;
  }
  ESP_LOGI(TAG, "Filesystem mounted");

  // Card has been initialized, print its properties
  sdmmc_card_print_info(stdout, card);

  showFatFsInfo();

  return true;
}

/**
 * @brief 获取SD卡的总容量大小
 *
 * 通过计算SD卡的扇区数量和扇区大小来获取总容量
 *
 * @return size_t 返回SD卡的总容量大小，单位为字节
 */
size_t getSDCardSize(void)
{
  return card->csd.capacity * card->csd.sector_size; // 返回SD卡的总大小（字节）
}

/**
 * @brief 获取SD卡类型
 *
 * 根据SD卡的属性判断其具体类型，支持以下类型检测：
 * - SDIO类型卡：使用SDIO接口的设备
 * - MMC类型卡：多媒体卡
 * - SDSC (标准容量SD卡)：容量最大2GB
 * - SDHC (高容量SD卡)：容量4GB-32GB
 * - SDHC/SDXC (UHS-I接口的高容量/扩展容量卡)：容量32GB-2TB，支持高速传输
 *
 * 该函数通过检查SD卡结构体中的标志位和OCR寄存器来判断卡的类型。
 *
 * @return char* 返回SD卡类型的字符串标识符，可能的值包括：
 *               "SDIO", "MMC", "SDSC", "SDHC", "SDHC/SDXC (UHS-I)"
 *               如果无法识别类型，则返回 "Unknown"
 *               注意：返回的字符串是静态常量，不需要释放内存
 */
char *getCardType()
{
  if (card->is_sdio)
  {
    return "SDIO";
  }
  else if (card->is_mmc)
  {
    return "MMC";
  }
  else
  {
    if ((card->ocr & SD_OCR_SDHC_CAP) == 0)
    {
      return "SDSC";
    }
    else
    {
      if (card->ocr & SD_OCR_S18_RA)
      {
        return "SDHC/SDXC (UHS-I)";
      }
      else
      {
        return "SDHC";
      }
    }
  }
  return "Unknown";
}

/**
 * @brief Deinitialize the storage component
 *
 * This function unmounts the FAT filesystem and deinitializes the SDMMC host.
 */
void sdcard_deinit(void)
{
  esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to unmount filesystem (%s)", esp_err_to_name(ret));
  }
  else
  {
    ESP_LOGI(TAG, "Filesystem unmounted successfully");
  }

  // Deinitialize the SDMMC host
  sdmmc_host_deinit();
  ESP_LOGI(TAG, "SDMMC host deinitialized");
}

/************   SD storage ***********************/
#define AVITEMP SD_MOUNT_POINT "/current.avi"
#define MIN_SECIBDS (5) // default min video length (includes POST_MOTION_TIME)
extern uint8_t aviHeader[];

// local variables for AVI file writing
static uint8_t iSDbuffer[(RAMSIZE + CHUNK_HDR) * 2] = {0};
static size_t highPoint = 0;
static FILE *aviFile = NULL;
static char partName[FILE_NAME_LEN] = {0};
static framesize_t fsizePtr;
static uint8_t l_FPS;

// header and reporting info
static uint32_t vidSize;   // 视频总大小
uint16_t frameCnt;         // 当前文件总帧数
static uint32_t startTime; // 打开文件完成的时间点 ms
uint32_t dTimeTot;         // 总算法处理时间(解码+运动分析) ms
static uint32_t fTimeTot;  // 总拷贝时间 ms
static uint32_t wTimeTot;  // 总写入时间 ms
static uint32_t oTime;     // 文件打开耗时 ms
static uint32_t cTime;     // 文件关闭耗时 ms
static uint32_t sTime;     // file streaming time

// status & control fields
extern bool doRecording; // whether to capture to SD or not

static bool haveSrt = false; // whether telemetry data is being recorded

SemaphoreHandle_t aviMutex = NULL;


// playback
SemaphoreHandle_t pbFileMutex = NULL;
static FILE *playBack_fp = NULL;
static size_t readLen = 0;
static char aviFileName[FILE_NAME_LEN] = {0};
static fnameStruct fnameMeta = {0};
static mjpegStruct mjpegData = {0};
SemaphoreHandle_t readSemaphore = NULL;
SemaphoreHandle_t playbackSemaphore = NULL;
static TaskHandle_t playbackHandle = NULL;
#define PLAYBACK_STACK_SIZE (1024 * 6)
#define PLAY_PRI 4

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
bool openAvi()
{
  // derive filename from date & time, store in date folder
  // time to open a new file on SD increases with the number of files already present
  oTime = esp_timer_get_time() / 1000;
  dateFormat(partName, sizeof(partName), true);
  if (access(partName, F_OK) != 0)
  {
    if (mkdir(partName, 0777) != 0)
    {
      ESP_LOGE(TAG, "Failed to create date folder");
      return false;
    }
    else
    {
      ESP_LOGI(TAG, "Created folder: %s", partName);
    }
  }
  dateFormat(partName, sizeof(partName), false);
  // open avi file with temporary name
  aviFile = fopen(AVITEMP, "wb+");
  if (aviFile == NULL)
  {
    ESP_LOGE(TAG, "Failed to open file for writing");
    return false;
  }
  oTime = esp_timer_get_time() / 1000 - oTime;
  ESP_LOGI(TAG, "File opening time: %ums", oTime);
#if INCLUDE_AUDIO
  startAudio();
#endif
#if INCLUDE_TELEM
  haveSrt = startTelemetry();
#endif
  // initialisation of counters
  startTime = esp_timer_get_time() / 1000;
  frameCnt = fTimeTot = wTimeTot = dTimeTot = vidSize = 0;
  highPoint = AVI_HEADER_LEN; // allot space for AVI header
  fsizePtr = get_camera_frame_size();
  prepAviIndex(false);
  return true;
}

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
void saveFrame(uint8_t *frame_buf, size_t len)
{
  // save frame on SD card
  uint32_t fTime = esp_timer_get_time() / 1000;
  // align end of jpeg on 4 byte boundary for AVI
  uint16_t filler = (4 - (len & 0x00000003)) & 0x00000003;
  size_t jpegSize = len + filler;
  // add avi frame header
  memcpy(iSDbuffer + highPoint, dcBuf, 4);
  memcpy(iSDbuffer + highPoint + 4, &jpegSize, 4);
  highPoint += CHUNK_HDR;
  if (highPoint >= RAMSIZE)
  {
    // marker overflows buffer
    highPoint -= RAMSIZE;
    fwrite(iSDbuffer, 1, RAMSIZE, aviFile);
    // push overflow to buffer start
    memcpy(iSDbuffer, iSDbuffer + RAMSIZE, highPoint);
  }
  // add frame content
  size_t jpegRemain = jpegSize;
  uint32_t wTime = esp_timer_get_time() / 1000;
  while (jpegRemain >= RAMSIZE - highPoint)
  {
    // write to SD when RAMSIZE is filled in buffer
    memcpy(iSDbuffer + highPoint, frame_buf + jpegSize - jpegRemain, RAMSIZE - highPoint);
    fwrite(iSDbuffer, 1, RAMSIZE, aviFile);
    jpegRemain -= RAMSIZE - highPoint;
    highPoint = 0;
  }
  wTime = esp_timer_get_time() / 1000 - wTime;
  wTimeTot += wTime;
  ESP_LOGD(TAG, "storage write to sdcard cost %u ms", wTime);
  // whats left or small frame
  memcpy(iSDbuffer + highPoint, frame_buf + jpegSize - jpegRemain, jpegRemain);
  highPoint += jpegRemain;

  buildAviIdx(jpegSize, true, false); // save avi index for frame
  vidSize += jpegSize + CHUNK_HDR;
  frameCnt++;
  fTime = esp_timer_get_time() / 1000 - fTime - wTime;
  fTimeTot += fTime;
  ESP_LOGD(TAG, "Frame saving time %u ms", fTime);
  ESP_LOGD(TAG, "============================");
}

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
bool closeAvi(char *fileName)
{
  // closes the recorded file
  char aviFileName[FILE_NAME_LEN] = {0};
  uint32_t vidDuration = esp_timer_get_time() / 1000 - startTime;
  uint32_t vidDurationSecs = lround(vidDuration / 1000.0);

  cTime = esp_timer_get_time() / 1000;
  // write remaining frame content to SD
  fwrite(iSDbuffer, 1, highPoint, aviFile);
  size_t readLen = 0;
  bool haveWav = false;
#if INCLUDE_AUDIO
  // add wav file if exists
  finishAudio(true);
  haveWav = haveWavFile();
  if (haveWav)
  {
    do
    {
      readLen = writeWavFile(iSDbuffer, RAMSIZE);
      fwrite(iSDbuffer, 1, readLen, aviFile);
    } while (readLen > 0);
  }
#endif
  // save avi index
  finalizeAviIndex(frameCnt, false);
  do
  {
    readLen = writeAviIndex(iSDbuffer, RAMSIZE, false);
    if (readLen)
    {
      fwrite(iSDbuffer, 1, readLen, aviFile);
    }
  } while (readLen > 0);
  // save avi header at start of file
  float actualFPS = (1000.0f * (float)frameCnt) / ((float)vidDuration);
  uint8_t actualFPSint = (uint8_t)(lround(actualFPS));
  xSemaphoreTake(aviMutex, portMAX_DELAY);
  buildAviHdr(actualFPSint, fsizePtr, frameCnt, false);
  xSemaphoreGive(aviMutex);
  fseek(aviFile, 0, SEEK_SET);
  fwrite(aviHeader, 1, AVI_HEADER_LEN, aviFile);
  fclose(aviFile);
  uint32_t hTime = esp_timer_get_time() / 1000;
  ESP_LOGI(TAG, "Final SD storage time %lu ms", (hTime - cTime));
  if (vidDurationSecs >= MIN_SECIBDS)
  {
    // name file to include actual dateTime, FPS, duration, and frame count

    int alen = snprintf(aviFileName, FILE_NAME_LEN - 1, "%s_%ux%u_%u_%lu%s%s.%s",
                        partName, frameData[fsizePtr].frameWidth, frameData[fsizePtr].frameHeight, actualFPSint, vidDurationSecs,
                        haveWav ? "_S" : "", haveSrt ? "_M" : "", AVI_EXT);
    if (alen > FILE_NAME_LEN - 1)
      ESP_LOGW(TAG, "file name truncated");
    if (rename(AVITEMP, aviFileName) != 0)
    {
      ESP_LOGI(TAG, "Rename %s to %s: %d", AVITEMP, aviFileName);
    }
    if (fileName)
    {
      strcpy(fileName, aviFileName);
    }
    uint32_t now = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "AVI close time %lu ms", (now - hTime));
    cTime = now - cTime;
#if INCLUDE_TELEM
    stopTelemetry(aviFileName);
#endif
    // AVI stats
    ESP_LOGI(TAG, "******** AVI recording stats ********");
    ESP_LOGI(TAG, "Recorded %s", aviFileName);
    ESP_LOGI(TAG, "AVI duration: %u secs. Number of frames: %u", vidDurationSecs, frameCnt);
    ESP_LOGI(TAG, "Required FPS: %u. Actual FPS: %0.1f", l_FPS, actualFPS);
    ESP_LOGI(TAG, "File size: %s", fmtSize(vidSize));
    if (frameCnt)
    {
      ESP_LOGI(TAG, "Average frame length: %u bytes", vidSize / frameCnt);
      ESP_LOGI(TAG, "Average frame monitoring time: %u ms", dTimeTot / frameCnt);
      ESP_LOGI(TAG, "Average frame buffering time: %u ms", fTimeTot / frameCnt);
      ESP_LOGI(TAG, "Average frame storage time: %u ms", wTimeTot / frameCnt);
    }
    ESP_LOGI(TAG, "Average SD write speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
    ESP_LOGI(TAG, "File open / completion times: %u ms / %u ms", oTime, cTime);
    ESP_LOGI(TAG, "Busy: %u%%", min(100 * (wTimeTot + fTimeTot + dTimeTot + oTime + cTime) / vidDuration, (uint32_t)100));
    checkMemory("closeAvi");
    ESP_LOGI(TAG, "*************************************");
#if INCLUDE_FTP_HFS
    if (autoUpload)
    {
      if (deleteAfter)
      {
        // issue #380 - in case other files failed to transfer, do whole parent folder
        dateFormat(partName, sizeof(partName), true);
        fsStartTransfer(partName);
      }
      else
        fsStartTransfer(aviFileName); // transfer this file to remote ftp server
    }
#endif
#if INCLUDE_TGRAM
    if (tgramUse)
      tgramAlert(aviFileName, "");
#endif
    if (!checkFreeStorage())
      doRecording = false;
    return true;
  }
  else
  {
    // delete too small files if exist
    remove(AVITEMP);
    ESP_LOGI(TAG, "Insufficient capture duration: %u secs", vidDurationSecs);
    return false;
  }
}



fnameStruct *playbackFPS(const char *fname)
{
  // extract meta data from filename to commence playback
  char fnameStr[FILE_NAME_LEN] = {0};
  strcpy(fnameStr, fname);
  // replace all '_' with space for sscanf
  replaceChar(fnameStr, '_', ' ');

  int items = sscanf(fnameStr, "%*s %*s %*s %hhu %lu", &fnameMeta.recFPS, &fnameMeta.recDuration);
  if (items != 2)
  {
    ESP_LOGE(TAG, "failed to parse %s, items %u", fname, items);
  }

  if (fnameMeta.recFPS < 1)
  {
    fnameMeta.recFPS = 1;
  }

  ESP_LOGI(TAG, "Playback FPS %u Duration %lu", fnameMeta.recFPS, fnameMeta.recDuration);

  return &fnameMeta;
}

bool openSDfile(const char *streamFile)
{
  // open selected file on SD for streaming
  strcpy(aviFileName, streamFile);
  ESP_LOGI(TAG, "Playing %s", aviFileName);
  xSemaphoreTake(pbFileMutex, portMAX_DELAY);
  if (!playBack_fp) {
    fclose(playBack_fp);
  }
  playBack_fp = fopen(aviFileName, "rb");
  if (NULL == playBack_fp)
  {
    ESP_LOGE(TAG, "Failed to open %s", aviFileName);
    xSemaphoreGive(pbFileMutex);
    return false;
  }
  fseek(playBack_fp, AVI_HEADER_LEN, SEEK_SET); // skip over header
  xSemaphoreGive(pbFileMutex);

  return true;
}

void readSD()
{
  // read next cluster from SD for playback
  xSemaphoreTake(pbFileMutex, portMAX_DELAY);
  if (!playBack_fp)
  {
    ESP_LOGE(TAG, "No file open for playback");
    xSemaphoreGive(pbFileMutex);
    return;
  }

  uint32_t rTime = esp_timer_get_time() / 1000;

  // read to interim dram before copying to psram
  readLen = 0;
  readLen = fread(iSDbuffer + RAMSIZE + CHUNK_HDR, 1, RAMSIZE, playBack_fp);
  xSemaphoreGive(pbFileMutex);

  uint32_t now = esp_timer_get_time() / 1000;
  ESP_LOGD(TAG, "SD read time %lu ms", now - rTime);
  wTimeTot += now - rTime;

  xSemaphoreGive(readSemaphore); // signal that ready
}

mjpegStruct *getNextFrame(bool firstCall, bool *stopPlayback)
{
  // get next cluster on demand when ready for opened avi
  static bool remainingBuff;
  static bool completedPlayback;
  static size_t buffOffset;
  static uint32_t hTimeTot;
  static uint32_t tTimeTot;
  static uint32_t hTime;
  static size_t remainingFrame;
  static size_t buffLen;
  uint32_t mTime, dt;
  const uint32_t dcVal = 0x63643030; // value of 00dc marker
  if (firstCall)
  {
    sTime = esp_timer_get_time() / 1000;
    hTime = sTime;
    remainingBuff = completedPlayback = false;
    frameCnt = remainingFrame = vidSize = buffOffset = 0;
    wTimeTot = fTimeTot = hTimeTot = tTimeTot = 1; // avoid divide by 0
    mjpegData.data = (char *)iSDbuffer;
  }
  mTime = esp_timer_get_time() / 1000;
  dt = mTime - sTime;
  ESP_LOGD(TAG, "http send time %lu ms", dt);
  hTimeTot += dt;

  if (!(*stopPlayback))
  {
    // continue sending out frames
    if (!remainingBuff)
    {
      // load more data from SD
      mTime = esp_timer_get_time() / 1000;
      // move final bytes to buffer start in case jpeg marker at end of buffer
      memcpy(iSDbuffer, iSDbuffer + RAMSIZE, CHUNK_HDR);
      xSemaphoreTake(readSemaphore, portMAX_DELAY); // wait for read from SD card completed
      buffLen = readLen;
      dt = esp_timer_get_time() / 1000 - mTime;
      ESP_LOGD(TAG, "SD wait time %lu ms", dt);
      wTimeTot += dt;

      mTime = esp_timer_get_time() / 1000;
      // overlap buffer by CHUNK_HDR to prevent jpeg marker being split between buffers
      memcpy(iSDbuffer + CHUNK_HDR, iSDbuffer + RAMSIZE + CHUNK_HDR, buffLen); // load new cluster from double buffer
      dt = esp_timer_get_time() / 1000 - mTime;
      ESP_LOGD(TAG, "memcpy took %lu ms for %u bytes", dt, buffLen);
      fTimeTot += dt;
      remainingBuff = true;
      if (buffOffset > RAMSIZE)
        buffOffset = 4; // special case, marker overlaps end of buffer
      else
        buffOffset = frameCnt ? 0 : CHUNK_HDR; // only before 1st frame
      xTaskNotifyGive(playbackHandle);         // wake up task to get next cluster - sets readLen
    }
    mTime = esp_timer_get_time() / 1000;
    if (!remainingFrame)
    {
      // at start of jpeg frame marker
      uint32_t inVal;
      memcpy(&inVal, iSDbuffer + buffOffset, 4);
      if (inVal != dcVal)
      {
        // reached end of frames to stream
        mjpegData.buffLen = buffOffset; // remainder of final jpeg
        mjpegData.buffOffset = 0;       // from start of buff
        mjpegData.jpegSize = 0;
        *stopPlayback = completedPlayback = true;
        goto closeFile;
      }
      else
      {
        // get jpeg frame size
        uint32_t jpegSize;
        memcpy(&jpegSize, iSDbuffer + buffOffset + 4, 4);
        remainingFrame = jpegSize;
        vidSize += jpegSize;
        buffOffset += CHUNK_HDR;       // skip over marker
        mjpegData.jpegSize = jpegSize; // signal start of jpeg to webServer
        mTime = esp_timer_get_time() / 1000;
        // wait on playbackSemaphore for rate control
        xSemaphoreTake(playbackSemaphore, portMAX_DELAY);
        dt = esp_timer_get_time() / 1000 - mTime;
        ESP_LOGD(TAG, "frame timer wait %lu ms", dt);
        tTimeTot += dt;
        frameCnt++;
      }
    }
    else
      mjpegData.jpegSize = 0; // within frame,
    // determine amount of data to send to webServer
    if (buffOffset > RAMSIZE)
      mjpegData.buffLen = 0; // special case
    else
      mjpegData.buffLen = (remainingFrame > buffLen - buffOffset) ? buffLen - buffOffset : remainingFrame;
    mjpegData.buffOffset = buffOffset; // from here
    remainingFrame -= mjpegData.buffLen;
    buffOffset += mjpegData.buffLen;
    if (buffOffset >= buffLen)
      remainingBuff = false;
      // return pointer to data
    hTime = esp_timer_get_time() / 1000;
    return &mjpegData;
  }

closeFile:
  // finished, close SD file used for streaming
  xSemaphoreTake(pbFileMutex, portMAX_DELAY);
  if (playBack_fp)
  {
    fclose(playBack_fp);
    playBack_fp = NULL;
  }
  xSemaphoreGive(pbFileMutex);

  if (!completedPlayback)
  {
    ESP_LOGI(TAG, "Force close playback");
  }
  uint32_t playDuration = (esp_timer_get_time() / 1000 - sTime) / 1000;
  uint32_t totBusy = wTimeTot + fTimeTot + hTimeTot;
  ESP_LOGI(TAG, "******** AVI playback stats ********");
  ESP_LOGI(TAG, "Playback %s", aviFileName);
  ESP_LOGI(TAG, "Recorded FPS %u, duration %u secs", fnameMeta.recFPS, fnameMeta.recDuration);
  ESP_LOGI(TAG, "Playback FPS %0.1f, duration %u secs", (float)frameCnt / playDuration, playDuration);
  ESP_LOGI(TAG, "Number of frames: %u", frameCnt);
  if (frameCnt)
  {
    ESP_LOGI(TAG, "Average SD read speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
    ESP_LOGI(TAG, "Average frame SD read time: %u ms", wTimeTot / frameCnt);
    ESP_LOGI(TAG, "Average frame processing time: %u ms", fTimeTot / frameCnt);
    ESP_LOGI(TAG, "Average frame delay time: %u ms", tTimeTot / frameCnt);
    ESP_LOGI(TAG, "Average http send time: %u ms", hTimeTot / frameCnt);
    ESP_LOGI(TAG, "Busy: %u%%", min(100 * totBusy / (totBusy + tTimeTot), (uint32_t)100));
  }
  checkMemory("getNextFrame");
  ESP_LOGI(TAG, "*************************************\n");

  mjpegData.buffLen = mjpegData.buffOffset = 0; // signal end of jpeg

  return &mjpegData;
}

static void playbackTask(void *parameter)
{
  while (true)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    readSD();
  }
  vTaskDelete(NULL);
}

bool storageInit()
{
  // initialisation & prep for AVI capture
  aviMutex = xSemaphoreCreateMutex();
  if (aviMutex == NULL)
  {
    ESP_LOGE(TAG, "Failed to create mutex");
    return false;
  }
  readSemaphore = xSemaphoreCreateBinary();
  if (readSemaphore == NULL)
  {
    ESP_LOGE(TAG, "Failed to create read semaphore");
    return false;
  }
  playbackSemaphore = xSemaphoreCreateBinary();
  if (playbackSemaphore == NULL)
  {
    ESP_LOGE(TAG, "Failed to create playback semaphore");
    return false;
  }

  pbFileMutex = xSemaphoreCreateMutex();
  xTaskCreate(&playbackTask, "playbackTask", PLAYBACK_STACK_SIZE, NULL, PLAY_PRI, &playbackHandle);

  debugMemory("storageInit");
  return true;
}

void storageSetFPS(uint8_t fps)
{
  l_FPS = fps;
}
