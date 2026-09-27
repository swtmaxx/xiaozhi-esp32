# AlphaPi One S

This board definition targets the AlphaPi One S ESP32-S2R2 module.

Hardware used by this port:

- ESP32-S2R2 with 2 MB Quad PSRAM and 8 MB flash
- 128x160 ST7735 display driven through the ESP-IDF ST7789 API
- Display SPI: SCLK GPIO37, MOSI GPIO38, MISO GPIO45, CS GPIO34, DC GPIO36, RESET GPIO35
- Backlight GPIO33, active high
- Buttons A/B/C/D on GPIO13/12/11/10, active low
- N32 audio peripheral on UART1, TX GPIO3, RX GPIO0, 460800 baud

Build the variant from the repository root with:

```text
python scripts/build.py alphapi/alphapi-one-s --name alphapi-one-s
```

The audio codec is implemented in `alphapi_one_audio_codec.cc`. It keeps the
UART framing and 8-bit WAV/PCM conversion local to this board so the common
audio service continues to exchange 16-bit mono PCM.
