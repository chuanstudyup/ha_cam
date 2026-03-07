#include "mic.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "g711_pcm_convert.h"

#define TAG "Mic"

// MSM261S4030H0R

#define MIC_STD_BCLK_IO1        18      // I2S bit clock io number
#define MIC_STD_WS_IO1          39      // I2S word select io number
#define MIC_STD_DOUT_IO1        I2S_GPIO_UNUSED     // I2S data out io number
#define MIC_STD_DIN_IO1         40     // I2S data in io number

static i2s_chan_handle_t                rx_chan = NULL;        // I2S rx channel handler

void mic_i2s_std_init(void)
{
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));
    i2s_std_config_t rx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SMPLING_RATE),
        .slot_cfg  = {
            .data_bit_width = MIC_DATA_BIT_WIDTH, 
            .slot_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_RIGHT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // some codecs may require mclk signal, this example doesn't need it
            .bclk = MIC_STD_BCLK_IO1,
            .ws   = MIC_STD_WS_IO1,
            .dout = MIC_STD_DOUT_IO1,
            .din  = MIC_STD_DIN_IO1,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg));

    /* Enable the RX channel */
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

/**
 * @brief Read data from the microphone: 16-bits PCM, little-endian
 * 
 * @param buf Pointer to the buffer where the read data will be stored
 * @param size Size of the buffer in bytes
 * @return Number of bytes read, or -1 on error
 */
int mic_read(uint8_t *buf, size_t size)
{
    size_t r_bytes = 0;
    /* Read i2s data */
    esp_err_t ret = i2s_channel_read(rx_chan, buf, size, &r_bytes, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        return -1;
    }
    return r_bytes; // Return the number of bytes read
}

/**
 * @brief Read data from the microphone in PCMA format
 * 
 * @param buf Pointer to the buffer where the read data will be stored
 * @param size Size of the buffer in bytes
 * @return Number of bytes of pcma, if success half of input size or -1 on error
 */
int mic_read_pcma(uint8_t *buf, size_t size)
{
    size_t r_bytes = 0;
    /* Read i2s data */
    esp_err_t ret = i2s_channel_read(rx_chan, buf, size, &r_bytes, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        return -1;
    }
    // Convert 16-bit PCM data to 8-bit PCMA format
    r_bytes = G711EnCode((char *)buf, (char *)buf, r_bytes, G711ALAW);
    return r_bytes; // Return the number of bytes read
}

/**
 * @brief Read data from the microphone in PCMU format
 * 
 * @param buf Pointer to the buffer where the read data will be stored
 * @param size Size of the buffer in bytes
 * @return Number of bytes of pcmu, if success half of input size or -1 on error
 */
int mic_read_pcmu(uint8_t *buf, size_t size)
{
    size_t r_bytes = 0;
    /* Read i2s data */
    esp_err_t ret = i2s_channel_read(rx_chan, buf, size, &r_bytes, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        return -1;
    }
    // Convert 16-bit PCM data to 8-bit PCMA format
    r_bytes = G711EnCode((char *)buf, (char *)buf, r_bytes, G711ULAW);
    return r_bytes; // Return the number of bytes read
}