#ifndef _CHINPINFO_H_
#define _CHINPINFO_H_

#include "esp_err.h"

void chip_info(void);
void debugMemory(const char* caller);
void checkMemory(const char* source);
esp_err_t initCpuTemperature(void);
esp_err_t getCpuTemperature(float *out_celsius);
esp_err_t deinitCpuTemperature(void);

#endif /* _CHINPINFO_H_ */
