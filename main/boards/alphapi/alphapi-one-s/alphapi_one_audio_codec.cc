#include "alphapi_one_audio_codec.h"

#include <driver/uart.h>
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cstring>

#define TAG "AlphaPiOneAudio"

namespace {

void PutLe16(uint8_t* dest, uint16_t value) {
    dest[0] = static_cast<uint8_t>(value & 0xff);
    dest[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void PutLe32(uint8_t* dest, uint32_t value) {
    dest[0] = static_cast<uint8_t>(value & 0xff);
    dest[1] = static_cast<uint8_t>((value >> 8) & 0xff);
    dest[2] = static_cast<uint8_t>((value >> 16) & 0xff);
    dest[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

}  // namespace

AlphaPiOneAudioCodec::AlphaPiOneAudioCodec(
    int input_sample_rate, int output_sample_rate, uart_port_t uart_port,
    int baud_rate, gpio_num_t tx_pin, gpio_num_t rx_pin)
    : uart_port_(uart_port),
      baud_rate_(baud_rate),
      tx_pin_(tx_pin),
      rx_pin_(rx_pin) {
    duplex_ = false;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_channels_ = 1;
    output_channels_ = 1;

    uart_config_t uart_config = {
        .baud_rate = baud_rate_,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(uart_port_, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_port_, tx_pin_, rx_pin_,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_port_, 4096, 4096, 0, nullptr, 0));
    uart_flush_input(uart_port_);

    ESP_LOGI(TAG, "UART codec initialized on port %d at %d baud",
             static_cast<int>(uart_port_), baud_rate_);
}

AlphaPiOneAudioCodec::~AlphaPiOneAudioCodec() {
    std::lock_guard<std::mutex> lock(uart_mutex_);
    if (uart_is_driver_installed(uart_port_)) {
        uart_driver_delete(uart_port_);
    }
}

void AlphaPiOneAudioCodec::Start() {
    AudioCodec::Start();
    std::lock_guard<std::mutex> lock(uart_mutex_);
    const auto volume = static_cast<uint8_t>(std::clamp(output_volume_, 0, 100));
    SendWriteFrameLocked(kAudioVolumeAddress, &volume, 1);
}

void AlphaPiOneAudioCodec::SetOutputVolume(int volume) {
    volume = std::clamp(volume, 0, 100);
    {
        std::lock_guard<std::mutex> lock(uart_mutex_);
        const auto value = static_cast<uint8_t>(volume);
        SendWriteFrameLocked(kAudioVolumeAddress, &value, 1);
    }
    AudioCodec::SetOutputVolume(volume);
}

void AlphaPiOneAudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(uart_mutex_);
    if (enable == input_enabled_) {
        return;
    }
    SendAudioControlLocked(enable ? 1 : 0);
    AudioCodec::EnableInput(enable);
}

void AlphaPiOneAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(uart_mutex_);
    if (enable == output_enabled_) {
        return;
    }
    if (enable) {
        SendAudioControlLocked(0);
        SendWavHeaderLocked();
    } else {
        SendAudioControlLocked(0);
        wav_header_sent_ = false;
    }
    AudioCodec::EnableOutput(enable);
}

int AlphaPiOneAudioCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_ || samples <= 0) {
        return 0;
    }

    const size_t requested = std::min<size_t>(samples, kMaxFrameData);
    std::vector<uint8_t> pcm;
    std::lock_guard<std::mutex> lock(uart_mutex_);
    if (!SendReadRequestLocked(kAudioReadAddress, requested) ||
        !ReadResponseLocked(kAudioReadAddress, pcm)) {
        return 0;
    }

    const size_t count = std::min<size_t>(pcm.size(), samples);
    for (size_t i = 0; i < count; ++i) {
        dest[i] = static_cast<int16_t>((static_cast<int>(pcm[i]) - 128) << 8);
    }
    return static_cast<int>(count);
}

int AlphaPiOneAudioCodec::Write(const int16_t* data, int samples) {
    if (!output_enabled_ || samples <= 0) {
        return samples;
    }

    std::lock_guard<std::mutex> lock(uart_mutex_);
    if (!wav_header_sent_) {
        SendWavHeaderLocked();
    }

    std::array<uint8_t, kMaxFrameData> pcm{};
    int offset = 0;
    while (offset < samples) {
        const size_t count = std::min<size_t>(kMaxFrameData, samples - offset);
        for (size_t i = 0; i < count; ++i) {
            pcm[i] = static_cast<uint8_t>(
                (static_cast<int32_t>(data[offset + i]) + 32768) >> 8);
        }
        if (!SendWriteFrameLocked(kAudioStreamAddress, pcm.data(), count)) {
            break;
        }
        offset += static_cast<int>(count);
    }
    return offset;
}

bool AlphaPiOneAudioCodec::SendWriteFrameLocked(uint8_t address,
                                                const uint8_t* data,
                                                size_t length) {
    if (length > 255) {
        return false;
    }

    std::vector<uint8_t> frame(4 + length);
    frame[0] = kWriteCommand;
    frame[1] = address;
    frame[2] = static_cast<uint8_t>(length);
    if (length > 0) {
        std::memcpy(frame.data() + 3, data, length);
    }
    uint8_t checksum = 0;
    for (size_t i = 0; i < frame.size() - 1; ++i) {
        checksum = static_cast<uint8_t>(checksum + frame[i]);
    }
    frame.back() = checksum;

    const int written = uart_write_bytes(uart_port_, frame.data(), frame.size());
    if (written != static_cast<int>(frame.size())) {
        ESP_LOGW(TAG, "UART write failed for address 0x%02x", address);
        return false;
    }
    return uart_wait_tx_done(uart_port_, pdMS_TO_TICKS(kResponseTimeoutMs)) == ESP_OK;
}

bool AlphaPiOneAudioCodec::SendReadRequestLocked(uint8_t address, size_t length) {
    if (length == 0 || length > 255) {
        return false;
    }
    const uint8_t request[] = {kReadCommand, address, static_cast<uint8_t>(length)};
    const int written = uart_write_bytes(uart_port_, request, sizeof(request));
    if (written != sizeof(request)) {
        return false;
    }
    return uart_wait_tx_done(uart_port_, pdMS_TO_TICKS(kResponseTimeoutMs)) == ESP_OK;
}

bool AlphaPiOneAudioCodec::ReadExactLocked(uint8_t* data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const int received = uart_read_bytes(
            uart_port_, data + offset, length - offset,
            pdMS_TO_TICKS(kResponseTimeoutMs));
        if (received <= 0) {
            return false;
        }
        offset += static_cast<size_t>(received);
    }
    return true;
}

bool AlphaPiOneAudioCodec::ReadResponseLocked(uint8_t expected_address,
                                              std::vector<uint8_t>& data) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        uint8_t header = 0;
        if (!ReadExactLocked(&header, 1)) {
            return false;
        }

        if (header == kWriteResponse) {
            uint8_t ack[2] = {};
            if (!ReadExactLocked(ack, sizeof(ack))) {
                return false;
            }
            continue;
        }
        if (header != kReadResponse) {
            continue;
        }

        uint8_t response_header[2] = {};
        if (!ReadExactLocked(response_header, sizeof(response_header))) {
            return false;
        }
        const uint8_t address = response_header[0];
        const uint8_t length = response_header[1];
        std::vector<uint8_t> payload(length);
        if (!ReadExactLocked(payload.data(), payload.size())) {
            return false;
        }
        uint8_t checksum = 0;
        if (!ReadExactLocked(&checksum, 1)) {
            return false;
        }

        uint8_t expected_checksum = kReadResponse;
        expected_checksum = static_cast<uint8_t>(expected_checksum + address + length);
        for (uint8_t value : payload) {
            expected_checksum = static_cast<uint8_t>(expected_checksum + value);
        }
        if (checksum != expected_checksum) {
            ESP_LOGW(TAG, "Invalid UART response checksum for address 0x%02x", address);
            continue;
        }
        if (address != expected_address) {
            continue;
        }
        data = std::move(payload);
        return true;
    }
    return false;
}

void AlphaPiOneAudioCodec::SendWavHeaderLocked() {
    if (wav_header_sent_) {
        return;
    }

    std::array<uint8_t, 44> header{};
    std::memcpy(header.data(), "RIFF", 4);
    PutLe32(header.data() + 4, 0xffffffff);
    std::memcpy(header.data() + 8, "WAVEfmt ", 8);
    PutLe32(header.data() + 16, 16);
    PutLe16(header.data() + 20, 1);
    PutLe16(header.data() + 22, 1);
    PutLe32(header.data() + 24, output_sample_rate_);
    PutLe32(header.data() + 28, output_sample_rate_);
    PutLe16(header.data() + 32, 1);
    PutLe16(header.data() + 34, 8);
    std::memcpy(header.data() + 36, "data", 4);
    PutLe32(header.data() + 40, 0xffffffff);
    SendWriteFrameLocked(kAudioStreamAddress, header.data(), header.size());
    wav_header_sent_ = true;
}

void AlphaPiOneAudioCodec::SendAudioControlLocked(uint8_t value) {
    SendWriteFrameLocked(kAudioControlAddress, &value, 1);
}
