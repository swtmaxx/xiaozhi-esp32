# AlphaPi One S

This board definition targets the AlphaPi One S ESP32-S2R2 module.

Hardware used by this port:

- ESP32-S2R2 with 2 MB Quad PSRAM and 8 MB flash
- 128x160 ST7789 display (RGB565) on SPI2 at 26 MHz, rotation 1
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


UART protocol notes:

- Write frame: `0x90`, register, length, payload, checksum (sum of the preceding
  bytes, low 8 bits). The N32 answers with `0x91`, register, accepted count.
- Read frame: `0x80`, register, length; the reply is `0x81`, register, length,
  payload, checksum.
- Registers: `0x10` record control, `0x11` PCM capture, `0x14` volume level
  (0-6), `0x15` PCM playback (max 200 bytes per frame).
- Playback starts with a 44-byte WAV header on `0x15`, then 8-bit unsigned PCM
  converted from the 16-bit mono stream with `(sample + 32768) >> 8`.
- `0x0f` initialises the shared N32 peripheral bus and is sent once before the
  first playback or capture.
- Application volume is 0-100; the codec maps it onto the hardware 0-6 levels.