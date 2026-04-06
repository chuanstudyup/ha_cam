#include "paramCommon.h"

static bool rangeCheck(void *val, enum PARAM_TYPE type, void *arg)
{
    int tmp = 0;
    switch (type)
    {
    case PARAM_TYPE_INT32:
        tmp = *(int *)val;
        break;
    case PARAM_TYPE_UINT8:
        tmp = *(uint8_t *)val;
        break;
    case PARAM_TYPE_FLOAT:
        tmp = *(float *)val;
        break;
    default:
        return true;
    }
    RANGE *range = (RANGE *)arg;
    if (tmp < range->min || tmp > range->max)
    {
        return false;
    }
    return true;
}

static RANGE sen_range = {1, 10};
static RANGE min_seconds_range = {0, 20};
static RANGE night_switch_range = {0, 100};
static RANGE rtsp_port_range = {554, 65535};

static PARAM_DEF MOTION_DETECT_PARAM[] = {
    {"enable", PARAM_TYPE_BOOL, {.b = true}, NULL, NULL, 0},
    {"sensitivity", PARAM_TYPE_UINT8, {.u8 = 7}, rangeCheck, &sen_range, 0},
    {"night_switch", PARAM_TYPE_UINT8, {.u8 = 10}, rangeCheck, &night_switch_range, 0},
};

static PARAM_DEF STORAGE_PARAM[] = {
    {"auto_upload", PARAM_TYPE_BOOL, {.b = false}, NULL, NULL, 0},
    {"auto_delete", PARAM_TYPE_BOOL, {.b = true}, NULL, NULL, 0},
};

static PARAM_DEF RTSP_SERVER_PARAM[] = {
    {"enable", PARAM_TYPE_BOOL, {.b = true}, NULL, NULL, 0},
    {"user", PARAM_TYPE_STRING, {.str = ""}, NULL, NULL, 32},
    {"password", PARAM_TYPE_STRING, {.str = ""}, NULL, NULL, 32},
    {"port", PARAM_TYPE_INT32, {.i32 = 554}, rangeCheck, &rtsp_port_range, 0},
};

CONFIG_CENTER config[] = {
    {"motion_detect", MOTION_DETECT_PARAM, sizeof(MOTION_DETECT_PARAM) / sizeof(PARAM_DEF)},
    {"storage", STORAGE_PARAM, sizeof(STORAGE_PARAM) / sizeof(PARAM_DEF)},
    {"rtsp", RTSP_SERVER_PARAM, sizeof(RTSP_SERVER_PARAM) / sizeof(PARAM_DEF)}};