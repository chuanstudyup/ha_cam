#ifndef _CAMERA_H_
#define _CAMERA_H_

#include "esp_err.h"
#include "cam_info.h"
#include "esp_camera.h"

#define FB_CNT 3 // number of frame buffers

esp_err_t init_camera(void);
esp_err_t test_camera(void);
framesize_t get_camera_frame_size(void);
void get_camera_frame_dimension(int *width, int *height);

/**
 * @brief 获取传感器型号名称
 * 
 * 通过ESP32相机传感器接口获取当前相机的传感器信息，并返回传感器型号名称。
 * 
 * @return char* 指向传感器型号名称字符串的指针
 */
const char* get_seneor_model_name(void);

#endif /* _CAMERA_H_ */