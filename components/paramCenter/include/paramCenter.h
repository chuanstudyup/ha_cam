#ifndef __PARAM_CENTER_H__
#define __PARAM_CENTER_H__

#include "paramModel.h"

#define CONFIG_PATH "/sdcard/config/"

bool configCenterInit();

uint8_t get_param_uint8(enum CONFIG_MODULE module_index, uint32_t param_index);
int32_t get_param_int32(enum CONFIG_MODULE module_index, uint32_t param_index);
float get_param_float(enum CONFIG_MODULE module_index, uint32_t param_index);
bool get_param_bool(enum CONFIG_MODULE module_index, uint32_t param_index);
char* get_param_string(enum CONFIG_MODULE module_index, uint32_t param_index);

bool set_param_uint8(enum CONFIG_MODULE module_index, uint32_t param_index, uint8_t value, bool saveFlash);
bool set_param_int32(enum CONFIG_MODULE module_index, uint32_t param_index, int32_t value, bool saveFlash);
bool set_param_float(enum CONFIG_MODULE module_index, uint32_t param_index, float value, bool saveFlash);
bool set_param_bool(enum CONFIG_MODULE module_index, uint32_t param_index, bool value, bool saveFlash);
bool set_param_str(enum CONFIG_MODULE module_index, uint32_t param_index, char* value, bool saveFlash);
bool save_config(enum CONFIG_MODULE module_index); // 将指定模块的配置保存到flash
/**
 * @brief 获取指定模块的JSON字符串表示
 *
 * 将配置中心中指定模块的所有参数转换为JSON对象格式
 *
 * @param module_index 模块索引，指定要获取配置参数的模块
 * @return cJSON* 成功返回包含模块参数的JSON对象指针，失败或内存分配失败返回NULL
 */
cJSON *get_module_json_str(enum CONFIG_MODULE module_index);

/**
 * @brief 设置参数值
 *
 * @param module_index 模块索引，必须大于等于0且小于CONFIG_MAX
 * @param param_index 参数索引，必须大于等于0且小于模块参数数量
 * @param val_str 参数值字符串形式
 * @return true 设置成功
 * @return false 设置失败(参数索引无效/参数类型不匹配/校验失败)
 *
 * @note 参数类型支持:
 * - PARAM_TYPE_UINT8: 无符号8位整数
 * - PARAM_TYPE_INT32: 有符号32位整数
 * - PARAM_TYPE_BOOL: 布尔值("true"/"1"为真)
 * - PARAM_TYPE_FLOAT: 浮点数
 */
bool set_param_str_val(enum CONFIG_MODULE module_index, uint32_t param_index, char *val_str, bool saveFlash);



#endif
