#ifndef __PARAM_COMMON_H__
#define __PARAM_COMMON_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum PARAM_TYPE
{
    PARAM_TYPE_UINT8,
    PARAM_TYPE_INT32,
    PARAM_TYPE_BOOL,
    PARAM_TYPE_FLOAT,
    PARAM_TYPE_STRING,
    PARAM_TYPE_MAX,
};

typedef struct _PARAM_DEF
{
    char *name;
    enum PARAM_TYPE type;
    union
    {
        uint8_t u8;
        int32_t i32;
        bool b;
        float f;
        char *str;
    } val;
    bool (*check)(void *val, enum PARAM_TYPE type, void *arg);
    void *checkarg;
    int32_t strMaxLen; // for string type
} PARAM_DEF;

typedef struct _RANGE
{
    int min;
    int max;
} RANGE;

typedef struct _CONFIG_CENTER
{
    char *module_name;
    PARAM_DEF *param;
    int size;
} CONFIG_CENTER;

#endif