#ifndef _MOTIONDETECT_H_
#define _MOTIONDETECT_H_

#include "esp_camera.h"

/**
 * @brief 获取当前亮度值
 * 
 * @return uint8_t 返回当前计算的亮度值（0-255）
 */
uint8_t getLuma();

/*
 * 获取当前是否为夜间状态的标志
 *
 * @return bool 返回true表示夜间，false表示白天
 */
bool getNightStatus();

/* 设置日夜检测阈值 */
void setNightSwitch(uint8_t nightSwitch);

/**
 * @brief 检测图像中的运动
 * 
 * 通过比较当前帧与前一帧的差异来检测运动。将JPEG图像转换为RGB888或灰度位图，
 * 进行缩放处理，然后比较像素差异来判断是否有运动发生。
 * 
 * @param fb 摄像头帧缓冲区指针，包含当前帧的JPEG图像数据
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
bool checkMotion(bool motionStatus);

/**
 * @brief 监控帧率并决定是否进行运动检测
 * 
 * 根据当前是否正在捕获视频，动态调整运动检测的调用频率：
 * - 捕获状态下：每 moveStopSecs 秒检查一次运动停止
 * - 非捕获状态下：每秒检查 moveStartChecks 次运动开始
 * 
 * @param[in] capturing 是否处于视频捕获状态
 * @return bool 返回 true 表示需要调用 checkMotion() 进行运动检测，false 表示跳过本次检测
 */
bool doMonitor(bool capturing);

void startMotionDetectTask();
void stopMotionDetectTask();


#endif
