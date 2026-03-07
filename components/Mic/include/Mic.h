#ifndef __MIC_H__
#define __MIC_H__
#include <stdint.h>
#include <stddef.h>

#define MIC_DATA_BIT_WIDTH (16) // data width for I2S
#define MIC_SMPLING_RATE (8000) // 8kHz sampling rate

void mic_i2s_std_init(void);

/**
 * @brief Read data from the microphone: 16-bits PCM, little-endian
 * 
 * @param buf Pointer to the buffer where the read data will be stored
 * @param size Size of the buffer in bytes
 * @return Number of bytes read, or -1 on error
 */
int mic_read(uint8_t *buf, size_t size);

/**
 * @brief Read data from the microphone in PCMA format
 * 
 * @param buf Pointer to the buffer where the read data will be stored
 * @param size Size of the buffer in bytes
 * @return Number of bytes of pcma, if success half of input size or -1 on error
 */
int mic_read_pcma(uint8_t *buf, size_t size);

/**
 * @brief Read data from the microphone in PCMU format
 * 
 * @param buf Pointer to the buffer where the read data will be stored
 * @param size Size of the buffer in bytes
 * @return Number of bytes of pcmu, if success half of input size or -1 on error
 */
int mic_read_pcmu(uint8_t *buf, size_t size);

#endif // __MIC_H__