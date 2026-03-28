#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "cJSON.h"

#include "paramCenter.h"
#include "paramCommon.h"
#include "paramModel.h"

#define TAG "PARAM_CENTER"

extern CONFIG_CENTER config[];

static bool get_param(enum CONFIG_MODULE module_index, uint32_t param_index, void *val)
{
    CONFIG_CENTER *module = &config[module_index];
    if (param_index >= module->size)
    {
        return false;
    }

    PARAM_DEF *param = module->param + param_index;

    switch (param->type)
    {
    case PARAM_TYPE_UINT8:
        *((uint8_t *)val) = param->val.u8;
        break;
    case PARAM_TYPE_INT32:
        *((int32_t *)val) = param->val.i32;
        break;
    case PARAM_TYPE_BOOL:
        *((bool *)val) = param->val.b;
        break;
    case PARAM_TYPE_FLOAT:
        *((float *)val) = param->val.f;
        break;
    case PARAM_TYPE_STRING:
        *((char **)val) = param->val.str;
        break;
    default:
        return false;
    }
    return true;
}

uint8_t get_param_uint8(enum CONFIG_MODULE module_index, uint32_t param_index)
{
    uint8_t val;
    if (!get_param(module_index, param_index, &val))
    {
        return 0;
    }
    return val;
}

int32_t get_param_int32(enum CONFIG_MODULE module_index, uint32_t param_index)
{
    int32_t val;
    if (!get_param(module_index, param_index, &val))
    {
        return 0;
    }
    return val;
}

bool get_param_bool(enum CONFIG_MODULE module_index, uint32_t param_index)
{
    bool val;
    if (!get_param(module_index, param_index, &val))
    {
        return false;
    }
    return val;
}

float get_param_float(enum CONFIG_MODULE module_index, uint32_t param_index)
{
    float val;
    if (!get_param(module_index, param_index, &val))
    {
        return 0.0f;
    }
    return val;
}

char *get_param_string(enum CONFIG_MODULE module_index, uint32_t param_index)
{
    char *val;
    if (!get_param(module_index, param_index, &val))
    {
        return NULL;
    }
    return val;
}


/**
 * @brief 获取指定模块的JSON字符串表示
 *
 * 将配置中心中指定模块的所有参数转换为JSON对象格式
 *
 * @param module_index 模块索引，指定要获取配置参数的模块
 * @return cJSON* 成功返回包含模块参数的JSON对象指针，失败或内存分配失败返回NULL
 */
cJSON *get_module_json_str(enum CONFIG_MODULE module_index)
{
    CONFIG_CENTER *module = config + module_index;
    double val = 0;

    cJSON *root = cJSON_CreateObject();
    cJSON *opts = cJSON_CreateObject();
    if (root == NULL || opts == NULL)
    {
        return NULL;
    }
    cJSON_AddItemToObject(root, module->module_name, opts);
    for (int i = 0; i < module->size; ++i)
    {
        PARAM_DEF *param = module->param + i;
        switch (param->type)
        {
        case PARAM_TYPE_STRING:
            cJSON_AddStringToObject(opts, param->name, param->val.str);
            break;
        case PARAM_TYPE_BOOL:
            cJSON_AddBoolToObject(opts, param->name, param->val.b);
            break;
        case PARAM_TYPE_UINT8:
            val = (double)param->val.u8;
            cJSON_AddNumberToObject(opts, param->name, val);
            break;
        case PARAM_TYPE_INT32:
            val = (double)param->val.i32;
            cJSON_AddNumberToObject(opts, param->name, val);
            break;
        case PARAM_TYPE_FLOAT:
            cJSON_AddNumberToObject(opts, param->name, param->val.f);
            break;
        default:
            continue;
        }
    }

    return root;
}

bool save_config(enum CONFIG_MODULE module_index)
{
    CONFIG_CENTER *module = config + module_index;
    char filename[64] = {0};

    if (0 != access(CONFIG_PATH, F_OK))
    {
        mkdir(CONFIG_PATH, 0777);
    }

    cJSON *root = get_module_json_str(module_index);
    if (root == NULL)
    {
        return false;
    }
    char *json_str = cJSON_Print(root);

    snprintf(filename, 64, "%s%s.json", CONFIG_PATH, module->module_name);
    FILE *fp = fopen(filename, "w");
    if (fp == NULL)
    {
        cJSON_Delete(root);
        return false;
    }

    fwrite(json_str, strlen(json_str), 1, fp);
    fclose(fp);
    cJSON_Delete(root);

    return true;
}

static bool load_config(enum CONFIG_MODULE module_index)
{
    CONFIG_CENTER *module = config + module_index;
    char filename[64] = {0};
    snprintf(filename, 64, "%s%s.json", CONFIG_PATH, module->module_name);

    /* 如果没有就保存创建一份 */
    if (0 != access(filename, F_OK))
    {
        save_config(module_index);
        ESP_LOGE(TAG, "No config file found. Create a new one.");
        return true;
    }
    else
    {
        FILE *fp = fopen(filename, "r");
        if (fp == NULL)
        {
            return false;
        }
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char *buf = (char *)malloc(len + 1);
        fread(buf, len, 1, fp);
        fclose(fp);

        cJSON *root = cJSON_Parse(buf);
        if (root == NULL)
        {
            free(buf);
            return false;
        }
        free(buf);

        cJSON *opts = cJSON_GetObjectItemCaseSensitive(root, module->module_name);
        if (opts == NULL)
        {
            cJSON_Delete(root);
            return false;
        }

        for (int i = 0; i < module->size; ++i)
        {
            PARAM_DEF *param = module->param + i;
            cJSON *item = cJSON_GetObjectItemCaseSensitive(opts, param->name);
            if (item == NULL)
            {
                continue;
            }
            switch (param->type)
            {
            case PARAM_TYPE_STRING:
                if (cJSON_IsString(item) && item->valuestring != NULL)
                {
                    if (strlen(param->val.str) < param->strMaxLen)
                    {
                        strcpy(param->val.str, item->valuestring);
                    }
                    else
                    {
                        free(param->val.str);
                        param->val.str = (char *)malloc(strlen(item->valuestring) + 1);
                        strcpy(param->val.str, item->valuestring);
                    }
                }
                break;
            case PARAM_TYPE_BOOL:
                if (cJSON_IsBool(item))
                {
                    param->val.b = cJSON_IsTrue(item);
                }
                break;
            case PARAM_TYPE_UINT8:
                if (cJSON_IsNumber(item))
                {
                    param->val.u8 = (uint8_t)cJSON_GetNumberValue(item);
                }
                break;
            case PARAM_TYPE_INT32:
                if (cJSON_IsNumber(item))
                {
                    param->val.i32 = (int32_t)cJSON_GetNumberValue(item);
                }
                break;
            case PARAM_TYPE_FLOAT:
                if (cJSON_IsNumber(item))
                {
                    param->val.f = (float)cJSON_GetNumberValue(item);
                }
                break;
            default:
                continue;
            }
        }
        cJSON_Delete(root);
    }
    return true;
}

bool configCenterInit()
{
    /* 初始化配置中心 */
    int i = 0, j = 0;
    CONFIG_CENTER *module = NULL;
    PARAM_DEF *param = NULL;
    char *p = NULL;

    /* 1. 为str类型参数分配内存 */
    for (i = 0; i < CONFIG_MAX; ++i)
    {
        module = config + i;
        for (j = 0; j < module->size; ++j)
        {
            param = module->param + j;
            if (param->type == PARAM_TYPE_STRING)
            {
                p = (char *)malloc(param->strMaxLen);
                if (param->val.str)
                {
                    strcpy(p, param->val.str);
                }
                param->val.str = p;
            }
        }
    }

    /* 2. 从json文件读取配置为内存 */
    for (i = 0; i < CONFIG_MAX; ++i)
    {
        load_config(i);
    }
    return true;
}

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
bool set_param_str_val(enum CONFIG_MODULE module_index, uint32_t param_index, char *val_str, bool saveFlash)
{
    CONFIG_CENTER *module = &config[module_index];
    if (param_index >= module->size)
    {
        return false;
    }

    PARAM_DEF *param = module->param + param_index;

    switch (param->type)
    {
    case PARAM_TYPE_UINT8:
    {
        uint8_t val = (uint8_t)atoi(val_str);
        if (param->check && !param->check(&val, param->type, param->checkarg))
        {
            return false;
        }
        param->val.u8 = val;
        break;
    }
    case PARAM_TYPE_INT32:
    {
        int32_t val = (int32_t)atoi(val_str);
        if (param->check && !param->check(&val, param->type, param->checkarg))
        {
            return false;
        }
        param->val.i32 = val;
        break;
    }
    case PARAM_TYPE_BOOL:
    {
        bool val = (strcmp(val_str, "true") == 0) || (strcmp(val_str, "1") == 0);
        param->val.b = val;
        break;
    }
    case PARAM_TYPE_FLOAT:
    {
        float val = atof(val_str);
        if (param->check && !param->check(&val, param->type, param->checkarg))
        {
            return false;
        }
        param->val.f = val;
        break;
    }
    case PARAM_TYPE_STRING:
        if (strlen(val_str) < param->strMaxLen)
        {
            strcpy(param->val.str, val_str);
        }
        else
        {
            return false;
        }
        break;
    default:
        return false;
    }

    if (saveFlash)
    {
        save_config(module_index);
    }

    return true;
}

static bool set_param(enum CONFIG_MODULE module_index, uint32_t param_index, void *val, bool saveFlash)
{
    CONFIG_CENTER *module = &config[module_index];
    if (param_index >= module->size)
    {
        return false;
    }

    PARAM_DEF *param = module->param + param_index;

    switch (param->type)
    {
    case PARAM_TYPE_UINT8:
        if (param->check && !param->check(val, param->type, param->checkarg))
        {
            return false;
        }
        param->val.u8 = *(uint8_t *)val;
        break;
    case PARAM_TYPE_INT32:
        if (param->check && !param->check(val, param->type, param->checkarg))
        {
            return false;
        }
        param->val.i32 = *(int32_t *)val;
        break;
    case PARAM_TYPE_BOOL:
        param->val.b = *(bool *)val;
        break;
    case PARAM_TYPE_FLOAT:
        if (param->check && !param->check(val, param->type, param->checkarg))
        {
            return false;
        }
        param->val.f = *(float *)val;
        break;
    case PARAM_TYPE_STRING:
        if (strlen((char *)val) < param->strMaxLen)
        {
            strcpy(param->val.str, (char *)val);
        }
        else
        {
            return false;
        }
        break;
    default:
        return false;
    }

    if (saveFlash)
    {
        save_config(module_index);
    }

    return true;
}

bool set_param_uint8(enum CONFIG_MODULE module_index, uint32_t param_index, uint8_t val, bool saveFlash)
{
    return set_param(module_index, param_index, &val, saveFlash);
}

bool set_param_int32(enum CONFIG_MODULE module_index, uint32_t param_index, int32_t val, bool saveFlash)
{
    return set_param(module_index, param_index, &val, saveFlash);
}

bool set_param_float(enum CONFIG_MODULE module_index, uint32_t param_index, float val, bool saveFlash)
{
    return set_param(module_index, param_index, &val, saveFlash);
}

bool set_param_str(enum CONFIG_MODULE module_index, uint32_t param_index, char *val, bool saveFlash)
{
    return set_param(module_index, param_index, val, saveFlash);
}

bool set_param_bool(enum CONFIG_MODULE module_index, uint32_t param_index, bool val, bool saveFlash)
{
    return set_param(module_index, param_index, &val, saveFlash);
}