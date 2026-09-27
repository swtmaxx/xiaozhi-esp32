#ifndef _ALPHAPI_ONE_AUDIO_CODEC_H_
#define _ALPHAPI_ONE_AUDIO_CODEC_H_

#include "audio_codec.h"

#include <driver/gpio.h>
#include <driver/uart.h>

#include <mutex>
#include <vector>

class AlphaPiOneAudioCodec : public AudioCodec {
public:
    AlphaPiOneAudioCodec(int input_sample_rate, int output_sample_rate,
                         uart_port_t uart_port, int baud_rate,
                         gpio_num_t tx_pin, gpio_num_t rx_pin);
    ~AlphaPiOneAudioCodec() override;

    void Start() override;
    void SetOutputVolume(int volume) override;
    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;

protected:
    int Read(int16_t* dest, int samples) override;
    int Write(const int16_t* data, int samples) override;

private:
    static constexpr uint8_t kReadCommand = 0x80;
    static constexpr uint8_t kReadResponse = 0x81;
    static constexpr uint8_t kWriteCommand = 0x90;
    static constexpr uint8_t kWriteResponse = 0x91;
    static constexpr uint8_t kAudioControlAddress = 0x10;
    static constexpr uint8_t kAudioReadAddress = 0x11;
    static constexpr uint8_t kAudioVolumeAddress = 0x14;
    static constexpr uint8_t kAudioStreamAddress = 0x15;
    // Shared UART peripheral bus initialisation command for the N32 coprocessor.
    static constexpr uint8_t kPeripheralInitAddress = 0x0f;
    // The hardware volume register takes a 0-6 level, not a 0-100 percentage.
    static constexpr uint8_t kHardwareVolumeMax = 6;
    // The frame length field is one byte, and N32 expects at most 200 bytes per
    // PCM frame, so keep the chunk size at or below both limits.
    static constexpr size_t kMaxFrameData = 200;
    static constexpr int kResponseTimeoutMs = 100;
    static constexpr int kWriteResponseTimeoutMs = 100;

    bool SendWriteFrameLocked(uint8_t address, const uint8_t* data, size_t length,
                              bool await_response = false);
    bool SendReadRequestLocked(uint8_t address, size_t length);
    bool ReadExactLocked(uint8_t* data, size_t length);
    bool ReadResponseLocked(uint8_t expected_address, std::vector<uint8_t>& data);
    void SendWavHeaderLocked();
    void SendAudioControlLocked(uint8_t value);
    void SendVolumeLocked(int volume);
    void InitializePeripheralLocked();

    uart_port_t uart_port_;
    int baud_rate_;
    gpio_num_t tx_pin_;
    gpio_num_t rx_pin_;
    bool wav_header_sent_ = false;
    bool peripheral_initialized_ = false;
    std::mutex uart_mutex_;
};

// Map the application's 0-100 volume onto the N32 hardware's 0-6 level.
uint8_t AlphaPiOneHardwareVolumeLevel(int volume);

#endif  // _ALPHAPI_ONE_AUDIO_CODEC_H_
