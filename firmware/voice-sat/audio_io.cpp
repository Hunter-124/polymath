#include "audio_io.h"
#include "audio_pins.h"
#include <driver/i2s.h>

namespace hearth {

static const i2s_port_t kMicPort = I2S_NUM_0;
static const i2s_port_t kSpkPort = I2S_NUM_1;

bool AudioIO::begin() {
    // --- mic: I2S RX, 16 kHz mono, 32-bit slot (ICS-43434 is 24-bit in 32) -----
    i2s_config_t rx = {};
    rx.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    rx.sample_rate = kSampleRate;
    rx.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    rx.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    rx.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    rx.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    rx.dma_buf_count = 4;
    rx.dma_buf_len = 256;
    if (i2s_driver_install(kMicPort, &rx, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t rxp = {};
    rxp.bck_io_num = MIC_I2S_BCLK; rxp.ws_io_num = MIC_I2S_LRCK;
    rxp.data_in_num = MIC_I2S_DIN; rxp.data_out_num = I2S_PIN_NO_CHANGE;
    i2s_set_pin(kMicPort, &rxp);

    // --- speaker: I2S TX, 16 kHz mono, 16-bit (MAX98357A) ----------------------
    i2s_config_t tx = {};
    tx.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    tx.sample_rate = kSampleRate;
    tx.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    tx.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    tx.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    tx.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    tx.dma_buf_count = 6;
    tx.dma_buf_len = 256;
    if (i2s_driver_install(kSpkPort, &tx, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t txp = {};
    txp.bck_io_num = SPK_I2S_BCLK; txp.ws_io_num = SPK_I2S_LRCK;
    txp.data_out_num = SPK_I2S_DOUT; txp.data_in_num = I2S_PIN_NO_CHANGE;
    i2s_set_pin(kSpkPort, &txp);
    return true;
}

size_t AudioIO::readMic(int16_t* out, size_t max) {
    // Read 32-bit frames, downshift to 16-bit. Read half as many 32-bit words.
    static int32_t raw[512];
    size_t want = max < 512 ? max : 512;
    size_t bytesRead = 0;
    i2s_read(kMicPort, raw, want * sizeof(int32_t), &bytesRead, 20 / portTICK_PERIOD_MS);
    size_t got = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < got; ++i) out[i] = (int16_t)(raw[i] >> 14);  // 24->16-ish
    return got;
}

void AudioIO::playPcm(const int16_t* samples, size_t n) {
    size_t written = 0;
    i2s_write(kSpkPort, samples, n * sizeof(int16_t), &written, portMAX_DELAY);
}

} // namespace hearth
