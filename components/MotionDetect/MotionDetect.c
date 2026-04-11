#include <stdio.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "MotionDetect.h"
#include "cam_info.h"
#include "Camera.h"
#include "ChipInfo.h"
#include "vCenter.h"
#include "esp_jpeg_dec.h"

#define ps_malloc(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM)

#define TAG "MotionDetect"

#define RESIZE_DIM 240                          // dimensions of resized motion bitmap
#define RESIZE_DIM_SQ (RESIZE_DIM * RESIZE_DIM) // pixels in bitmap
#define INACTIVE_COLOR 96                       // color for inactive motion pixel
#define JPEG_QUAL 80                            // % quality for generated motion detect jpeg
#define RGB888_BYTES 3                          // number of bytes per pixel
#define GRAYSCALE_BYTES 1                       // number of bytes per pixel
#define MOTION_DETECT_COLOR_DEPTH RGB888_BYTES  // GRAYSCALE_BYTES or RGB888_BYTES
#define RESIZE_DIM_LEN (RESIZE_DIM_SQ * MOTION_DETECT_COLOR_DEPTH)
static const int resizeDim_sq = RESIZE_DIM_SQ;
static const int resizeDimLen = RESIZE_DIM_LEN;

/* 亮度检测相关参数 */
#define DETECT_NIGHT_FRAMES 10 // frames of sequential darkness to avoid spurious day / night switching

// 运动检测相关参数
#define DETECT_MOTION_FRAMES 3     // 连续帧数检测到运动才认为有运动
#define DETECT_NUM_BANDS 10      // 检测区域划分成几个部分，划分成10部分（10行），则每部分的像素数为RESIZE_DIM*RESIZE_DIM/detectNumBands
#define DETECT_START_BAND 1        // 检测区域起始行，从1开始计数，1表示最顶部，10表示最底部
#define DETECT_END_BAND 9          // 检测区域结束行，从1开始计数
#define DETECT_CHANGE_THRESHOLD 15 // 像素变化阈值，大于此值认为是运动

static bool useMotion = false;          // 是否使用摄像头进行运动检测
static uint8_t motionVal = 8;          // 运动检测灵敏度，数值越大越敏感

static uint8_t *prevBuff = NULL;

/* 亮度检测相关变量 */
static uint8_t l_cur_luma = 0;     // current luma value
static uint8_t l_nightSwitch = 20; // luma threshold for night time detection
static bool nightTime = false;

/* 是否画面静止，经对比前后两帧 */
static bool l_still = false;

/**
 * @brief 获取当前亮度值
 *
 * @return uint8_t 返回当前计算的亮度值（0-255）
 */
uint8_t getLuma()
{
  return l_cur_luma;
}
/**
 * @brief 获取当前是否为夜间状态
 *
 * @return bool 返回true表示夜间，false表示白天
 */
bool getNightStatus()
{
  return nightTime;
}

/* 设置日夜检测阈值 */
void setNightSwitch(uint8_t nightSwitch)
{
  l_nightSwitch = nightSwitch;
}

uint8_t getNightSwitch()
{
  return l_nightSwitch;
}

/* 设置运动检测灵敏度 */
void setDetectSensitivity(uint8_t sensitivity)
{
  motionVal = sensitivity;
}

/* 获取运动检测灵敏度 */
uint8_t getDetectSensitivity()
{
  return motionVal;
}

/* 获取是否启用运动检测 */
bool getMotionDetectStatus()
{
  return useMotion;
}

/* 获取画面是否静止 */
bool getStillStatus()
{
  return l_still;
}

/* 更改运动检测状态 */
void changeMotionDetectStatus(bool status)
{
  if (status == useMotion)
  {
    return;
  }
  if (status)
  {
    startMotionDetectTask();
  }
  else
  {
    stopMotionDetectTask();
  }
  useMotion = status;
}

/* 亮度检测函数 */
static bool isNight(uint8_t luma)
{
  // check if night time for suspending recording
  // or for switching on lamp if enabled
  static uint16_t nightCnt = 0;
  l_cur_luma = luma;
  if (nightTime)
  {
    if (l_cur_luma > l_nightSwitch)
    {
      // light image
      nightCnt--;
      // signal day time after given sequence of light frames
      if (nightCnt == 0)
      {
        nightTime = false;
        ESP_LOGI(TAG, "Switch to Day time");
      }
    }
  }
  else
  {
    if (l_cur_luma < l_nightSwitch)
    {
      // dark image
      nightCnt++;
      // signal night time after given sequence of dark frames
      if (nightCnt > DETECT_NIGHT_FRAMES)
      {
        nightTime = true;
        ESP_LOGI(TAG, "Switch to Night time");
      }
    }
  }
  return nightTime;
}

/**
 * @brief 将JPEG图像解码为RGB888格式
 *
 * 该函数将输入的JPEG格式图像数据解码并转换为RGB888格式的图像数据。
 * 使用静态变量缓存解码器句柄和输出缓冲区以提高性能，仅在图像尺寸变化时重新初始化。
 * 输出图像将被缩放到RESIZE_DIM x RESIZE_DIM尺寸。
 *
 * @param input_buf 输入的JPEG数据缓冲区指针
 * @param len JPEG数据的长度
 * @param output_buf 输出RGB888数据缓冲区指针的指针（函数内部分配内存）
 * @param out_len 输出数据长度的指针
 * @param input_width 输入图像的宽度
 * @param input_height 输入图像的高度
 *
 * @return jpeg_error_t 返回解码状态码，JPEG_ERR_OK表示成功
 */
jpeg_error_t jgp2rgb888(uint8_t *input_buf, int len, uint8_t **output_buf, int *out_len, int input_width, int input_height)
{
  static uint8_t *out_buf = NULL;
  static size_t out_buf_size = 0;
  static jpeg_dec_handle_t jpeg_dec = NULL;
  static jpeg_dec_io_t *jpeg_io = NULL;
  static jpeg_dec_header_info_t *out_info = NULL;
  static int last_input_width = 0;
  static int last_input_height = 0;
  static int last_out_len = 0;
  static bool initialized = false;

  jpeg_error_t ret = JPEG_ERR_OK;

  if (input_width != last_input_width || input_height != last_input_height)
  {
    initialized = false;
    if (jpeg_dec)
    {
      jpeg_dec_close(jpeg_dec);
      jpeg_dec = NULL;
    }
    if (jpeg_io)
    {
      free(jpeg_io);
      jpeg_io = NULL;
    }
    if (out_info)
    {
      free(out_info);
      out_info = NULL;
    }
    last_input_width = input_width;
    last_input_height = input_height;

    // Generate default configuration
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.scale.width = RESIZE_DIM;
    config.scale.height = RESIZE_DIM;

    // Create jpeg_dec handle
    ret = jpeg_dec_open(&config, &jpeg_dec);
    if (ret != JPEG_ERR_OK)
    {
      return ret;
    }

    // Create io_callback handle
    jpeg_io = calloc(1, sizeof(jpeg_dec_io_t));
    if (jpeg_io == NULL)
    {
      ret = JPEG_ERR_NO_MEM;
      return ret;
    }

    // Create out_info handle
    out_info = calloc(1, sizeof(jpeg_dec_header_info_t));
    if (out_info == NULL)
    {
      ret = JPEG_ERR_NO_MEM;
      return ret;
    }

    // Set input buffer and buffer len to io_callback
    jpeg_io->inbuf = input_buf;
    jpeg_io->inbuf_len = len;

    // Parse jpeg picture header and get picture for user and decoder
    ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);
    if (ret != JPEG_ERR_OK)
    {
      return ret;
    }

    *out_len = out_info->width * out_info->height * 3;
    // Calloc out_put data buffer and update inbuf ptr and inbuf_len
    if (config.output_type == JPEG_PIXEL_FORMAT_RGB565_LE || config.output_type == JPEG_PIXEL_FORMAT_RGB565_BE || config.output_type == JPEG_PIXEL_FORMAT_CbYCrY)
    {
      *out_len = out_info->width * out_info->height * 2;
    }
    else if (config.output_type == JPEG_PIXEL_FORMAT_RGB888)
    {
      *out_len = out_info->width * out_info->height * 3;
    }
    else
    {
      ret = JPEG_ERR_INVALID_PARAM;
      return ret;
    }

    if (*out_len > out_buf_size)
    {
      if (out_buf)
      {
        free(out_buf);
      }
      out_buf = jpeg_calloc_align(*out_len, 16);
      if (out_buf == NULL)
      {
        ret = JPEG_ERR_NO_MEM;
        return ret;
      }
      out_buf_size = *out_len;
    }

    jpeg_io->outbuf = out_buf;
    *output_buf = out_buf;
    last_out_len = *out_len;

    initialized = true;
  }
  else
  {
    // Set input buffer and buffer len to io_callback
    jpeg_io->inbuf = input_buf;
    jpeg_io->inbuf_len = len;
    jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);

    *output_buf = out_buf;
    *out_len = last_out_len;
  }

  if (!initialized)
  {
    return JPEG_ERR_FAIL;
  }

  // Start decode jpeg
  ret = jpeg_dec_process(jpeg_dec, jpeg_io);

  return ret;
}

/**
 * @brief 检测图像中的运动
 *
 * 通过比较当前帧与前一帧的差异来检测运动。将JPEG图像转换为RGB888或灰度位图，
 * 进行缩放处理，然后比较像素差异来判断是否有运动发生。
 *
 * @param motionStatus 当前运动状态（true表示运动正在进行中，false表示无运动）
 *
 * @return bool 返回检测后的运动状态：
 *              - true: 检测到运动或运动正在进行中
 *              - false: 未检测到运动或夜间模式不检测
 *
 * @note 在夜间模式下（通过光照度判断）会强制返回false不检测运动
 * @note 支持调试模式生成运动变化映射图
 * @note 包含MQTT、SMTP、Telegram等外部服务集成
 */
bool checkMotion(bool motionStatus)
{
  // check difference between current and previous image
  int64_t tm1 = esp_timer_get_time();
  int64_t tm2 = 0;
  uint32_t lux = 0;
  static uint32_t motionCnt = 0;
  uint8_t *rgb_buf = NULL;
  int rgb_buf_len = 0;
  video_node *node = get_latest_video_frame();

  if (!node)
  {
    ESP_LOGW(TAG, "No video frame available for motion detection");
    return motionStatus;
  }
  int originWidth = node->width;
  int originHeight = node->height;

  if (PIXFORMAT_JPEG == node->format)
  {
    if (JPEG_ERR_OK != jgp2rgb888(node->data, node->size, &rgb_buf, &rgb_buf_len, originWidth, originHeight)) // convert image from JPEG to downscaled RGB888
    {
      ESP_LOGE(TAG, "jgp2raw() failure");
      return motionStatus;
    }
  }
  else
  {
    ESP_LOGE(TAG, "Unsupported pixel format %d for motion detection", node->format);
    return motionStatus;
  }
  put_video_frame(node);

  tm2 = esp_timer_get_time();
  int dt = tm2 - tm1;
  ESP_LOGD(TAG, "JPEG(%dx%d)[%d] convert to raw(%dx%d)[%d] in %lu us",
           originWidth, originHeight, node->size,
           RESIZE_DIM, RESIZE_DIM, rgb_buf_len,
           dt);

  if (prevBuff == NULL)
  {
    prevBuff = (uint8_t *)ps_malloc(resizeDim_sq);
  }

  tm1 = esp_timer_get_time();
  // compare each pixel in current frame with previous frame
  int changeCount = 0;
  // set horizontal region of interest in image
  uint32_t startPixel = resizeDimLen * (DETECT_START_BAND - 1) / DETECT_NUM_BANDS;
  uint32_t endPixel = resizeDimLen * (DETECT_END_BAND) / DETECT_NUM_BANDS;
  int moveThreshold = ((endPixel - startPixel) / MOTION_DETECT_COLOR_DEPTH) * (11 - motionVal) / 100; // number of changed pixels that constitute a movement
  for (int i = 0; i < resizeDimLen; i += MOTION_DETECT_COLOR_DEPTH)
  {
    uint16_t currPix = 0;
    uint16_t prePos = i / MOTION_DETECT_COLOR_DEPTH;
    for (int j = 0; j < MOTION_DETECT_COLOR_DEPTH; j++)
    {
      currPix += rgb_buf[i + j];
    }
    currPix /= MOTION_DETECT_COLOR_DEPTH;

    lux += currPix; // for calculating light level

    // determine pixel change status
    if (abs((int)currPix - (int)prevBuff[prePos]) > DETECT_CHANGE_THRESHOLD)
    {
      if (i > startPixel && i < endPixel)
      {
        changeCount++; // number of changed pixels
      }
    }
    prevBuff[prePos] = currPix; // save current pixel for next comparison
  }
  uint8_t luma = (lux * 100) / (resizeDim_sq * 255); // light value as a %
  nightTime = isNight(luma);
  tm2 = esp_timer_get_time();
  ESP_LOGD(TAG, "Detected %u changes, threshold %u, light level %u, in %lu us", changeCount, moveThreshold, luma, tm2 - tm1);

  if (changeCount > moveThreshold)
  {
    ESP_LOGI(TAG, "### Change detected");
    motionCnt++; // number of consecutive changes
    // need minimum sequence of changes to signal valid movement
    if (!motionStatus && motionCnt >= DETECT_MOTION_FRAMES)
    {
      ESP_LOGI(TAG, "***** Motion - START");
      motionStatus = true; // motion started
    }
    l_still = false;
  }
  else
  {
    motionCnt = 0;
    l_still = true;
  }

  if (motionStatus && !motionCnt)
  {
    // insufficient change or motion not classified
    ESP_LOGI(TAG, "***** Motion - STOP");
    motionStatus = false; // motion stopped
  }
  if (motionStatus)
    ESP_LOGI(TAG, "*** Motion - ongoing %u frames", motionCnt);

  // 晚上时间段不检测移动
  return nightTime ? false : motionStatus;
}

// motion detection parameters
#define moveStartChecks (5) // checks per second for start motion
#define moveStopSecs (2)    // secs between each check for stop, also determines post motion time
uint8_t FPS = 15;

/**
 * @brief 监控帧率并决定是否进行运动检测
 *
 * 根据当前是否正在捕获视频，动态调整运动检测的调用频率：
 * - 捕获状态下：每 moveStopSecs 秒检查一次运动停止
 * - 非捕获状态下：每秒检查 moveStartChecks 次运动开始
 *
 * @param[in] motioning 是否处于运动状态
 * @return bool 返回 true 表示需要调用 checkMotion() 进行运动检测，false 表示跳过本次检测
 */
bool doMonitor(bool motioning)
{
  // monitor incoming frames for motion
  static uint8_t motionCnt = 0;
  // ratio for monitoring stop during capture / movement prior to capture
  uint8_t checkRate = (motioning) ? FPS * moveStopSecs : FPS / moveStartChecks;
  if (!checkRate)
    checkRate = 1;
  if (++motionCnt / checkRate)
    motionCnt = 0; // time to check for motion

  return !(bool)motionCnt;
}

void motionDetectTask(void *pvParameters)
{
  bool motioning = false;
  while (1)
  {
    if (doMonitor(motioning))
    {
      motioning = checkMotion(motioning);
    }
    vTaskDelay(pdMS_TO_TICKS(1000 / FPS));
  }
}

static TaskHandle_t md_task_handle = NULL;

void startMotionDetectTask()
{
  if (md_task_handle)
  {
    ESP_LOGW(TAG, "Motion Detect Task already running");
    return;
  }
  xTaskCreate(motionDetectTask, "motionDetect", 4096, NULL, 1, &md_task_handle);
  ESP_LOGI(TAG, "Starting Motion Detect Task");
}

void stopMotionDetectTask()
{
  if (md_task_handle)
  {
    vTaskDelete(md_task_handle);
    md_task_handle = NULL;
    ESP_LOGI(TAG, "Stopping Motion Detect Task");
    return;
  }
}
