# Kittyface

Zigbee-triggered audio playback device. An ESP32-C6 acts as a Zigbee coordinator; when a paired Sonoff SNZB-01P button is pressed, it plays a spoken WAV clip ("Desi wants to go outside") through an I2S amplifier. The onboard LED shows pairing status.

## Hardware

| Part | Notes |
|---|---|
| ESP32-C6 DevKit | 4 MB flash, USB on `/dev/ttyACM0` (native USB serial/JTAG) |
| [NS4168 I2S audio amplifier module](https://github.com/nulllaborg/i2s_audio_amplifier_module) | 2.5 W mono class-D, 3.0-5.5 V, I2S input |
| Speaker | 4 ohm recommended (amp rated 2.5 W @ 5 V / 4 ohm) |
| Sonoff SNZB-01P | Zigbee wireless button (single / double / long press) |

### GPIO connections

| Signal | ESP32-C6 pin | Amp pin |
|---|---|---|
| I2S BCLK | GPIO21 | BCLK |
| I2S WS / LRCK | GPIO22 | LRCK |
| I2S data out | GPIO23 | DIN |
| Power | 5V | VCC (3V3 works but is quieter) |
| Ground | GND | GND |
| Status LED | GPIO8 (onboard WS2812) | - |
| Pairing / unpair button | GPIO9 (onboard BOOT) | - |

The I2S pins are configurable in `idf.py menuconfig` under *Kittyface Audio* (defaults above). The amp manufacturer's example uses IO33/32/23 on a classic ESP32; these are different because of the C6 devkit pinout.

Tip: mechanical resonance of the amp/speaker mounting can sound like scratchy noise. Mount the speaker firmly and keep I2S wires short.

## Behavior

- **Pairing:** opens automatically at boot when no button is paired (180 s window), or by holding BOOT for about 3 s and releasing. The first device to join during the window becomes the paired button, and the C6 binds its On/Off cluster to itself.
- **Playback:** any On / Off / Toggle command from the paired button (single, double or long press) plays `desi_outside.wav` once. Presses during playback are ignored. Commands from other devices are ignored.
- **Voice prompts** (played in order, queued behind anything already playing):

| Event | File |
|---|---|
| Boot | `welcome_msg.wav` |
| Pairing mode opened (auto at boot or 3 s BOOT hold) | `pairing_mode.wav` |
| Button joined | `pairing_successful.wav` |
| Pairing window closed with no device | `pairing_unsuccessful.wav` |
- **Unpair / full reset:** hold BOOT for about 10 s. This erases the stored button and Zigbee network state, then reboots (the welcome message plays again and the button must be re-paired).
- **BOOT hold cues:** one beep at 3 s means "release now to open pairing"; keep holding and a double beep at 10 s means the reset is about to happen.
- **Volume:** a short BOOT press (under 3 s) steps the volume 1 → 2 → 3 → 4 → 1. Each step plays `volume.wav` followed by `level_N.wav` at the new level. Volume is a software gain on all audio (levels 0.15 / 0.35 / 0.65 / 1.0), saved in NVS, default level 3.
- **Persistence:** the paired button's IEEE address is stored in NVS; the Zigbee network is restored on reboot.

### Status LED

| State | LED |
|---|---|
| Not paired | solid red |
| Pairing (permit-join open) | blinking blue |
| Paired | dim solid green |

## Software stack

- ESP-IDF **v6.1** (target `esp32c6`)
- IDF components: `esp_driver_i2s` (std mode, mono, 16-bit, no MCLK), `spiffs`, `nvs_flash`, `driver`
- Managed components (downloaded automatically by the IDF component manager, see [main/idf_component.yml](main/idf_component.yml)):
  - `espressif/esp-zigbee-lib` ~1.6.0
  - `espressif/esp-zboss-lib` ~1.6.0
  - `espressif/led_strip` ~3.0.0
- Zigbee: native 802.15.4 radio, coordinator role (`CONFIG_ZB_ZCZR`)
- Host tools for audio generation: `espeak-ng`, `ffmpeg` (optional)

## Layout

```
main/            firmware sources
  main.c           startup wiring
  audio_out.c      I2S output (tone + sample writes)
  wav_player.c     SPIFFS WAV parser/player task
  zb_coord.c       Zigbee coordinator, pairing, bind, press handling
  status_led.c     WS2812 status LED
  boot_button.c    BOOT button press/hold detection
spiffs/          WAV files flashed to the "audio" partition
audio_unused/    clips not yet used (desi_food.wav); unconverted/ holds the 22.05 kHz originals
partitions.csv   custom partition table (4 MB)
sdkconfig.defaults
```

Partitions: `nvs`, `phy_init`, `factory` app (1.5 MB), `zb_storage`, `zb_fct`, `audio` (SPIFFS, 1 MB).

## Build and flash

```
. ~/.espressif/tools/activate_idf_v6.1.sh
idf.py set-target esp32c6        # first time only
idf.py -p /dev/ttyACM0 flash monitor
```

Flashing also repacks `spiffs/` into the audio partition. The first build downloads the managed components and takes several minutes.

## Audio files

Files in `spiffs/` must be **WAV, PCM, mono, 16-bit, 16000 Hz**. Anything else is rejected with an error in the log.

Convert with ffmpeg:

```
ffmpeg -i input.mp3 -ac 1 -ar 16000 -c:a pcm_s16le spiffs/desi_outside.wav
```

Generate speech with espeak-ng:

```
espeak-ng -v en -s 140 -w /tmp/clip.wav "Desi wants to go outside"
ffmpeg -i /tmp/clip.wav -ac 1 -ar 16000 -c:a pcm_s16le spiffs/desi_outside.wav
```

Notes:
- The sample rate must match `KF_SAMPLE_RATE` (menuconfig, default 16000). Changing it changes the I2S clock for all files, so reconvert everything.
- The audio partition is 1 MB, about 32 s of audio at this format, shared by all files.
- Filenames must be 31 characters or fewer, with no subfolders.
- The firmware plays `desi_outside.wav` (`OUTSIDE_WAV` in `main/zb_coord.c`).
- Leave some headroom (e.g. `-af volume=0.8`); the amp is loud at full scale.

## Roadmap

- Play `audio_unused/desi_food.wav` for a second button / press type.
- Support multiple paired buttons, each mapped to its own clip (the paired device is already identified by IEEE address).
