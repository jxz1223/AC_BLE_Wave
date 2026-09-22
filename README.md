# AC BLE Wave

STM32WB55VGY6 four-channel AC sensor transmitter firmware. This repository is the verified final transmitter source for the four-channel board. It retains the STM32WB BLE stack and CubeMX project files, while the application layer is reduced to ADC acquisition, per-channel automatic gain control, packetization, and BLE notification.

## Current release

The release firmware is [firmware/AC_BLE_Wave_4CH_Center_Normalized_Final.hex](firmware/AC_BLE_Wave_4CH_Center_Normalized_Final.hex).

- Target: STM32WB55VGY6
- Sampling: four ADC channels scanned round-robin, 2000 S/s per channel
- Analog front end: four independent AD8231 channels, gains 1, 2, 4, 8, 16, 32, 64, 128
- Gain control: independent per channel, approximately 120 ms evaluation window
- SHA-256: `3A4889C367077CAF4AC28A7F7F4DC106BADBF4F993D0EAEC60B57DFF23CD0BD7`

## Project layout

| Path | Purpose |
| --- | --- |
| `STM32_WPAN/App/sensor_app.c` | Four-channel acquisition, automatic gain control, normalization, BLE payload creation |
| `STM32_WPAN/App/custom_app.c` | BLE GATT notification integration |
| `Core/`, `Drivers/`, `Middlewares/`, `Utilities/`, `STM32_WPAN/` | STM32Cube firmware, STM32WB BLE stack, and board project source |
| `BLE_VGY6.ioc` | STM32CubeMX configuration |
| `firmware/` | Verified flashable release image |

## Hardware mapping

| Channel | Instrument amplifier | ADC input | A0 | A1 | A2 |
| --- | --- | --- | --- | --- | --- |
| CH1 | U4 | PC0 | PB13 | PA1 | PA2 |
| CH2 | U5 | PC1 | PA3 | PA4 | PA5 |
| CH3 | U6 | PC2 | PD7 | PA7 | PB3 |
| CH4 | U7 | PC3 | PC6 | PC7 | PC8 |

The gain-control pins encode one of eight AD8231 gain settings. The ADC channels are sampled sequentially in a fixed CH1, CH2, CH3, CH4 order.

## Data processing and BLE payload

Each channel maintains its own ADC midpoint calibration and gain state. A 12-bit ADC sample is normalized into the gain-1 equivalent 19-bit domain:

```text
normalized = 262144 + (raw_adc - adc_center[channel]) * 128 / gain
```

So a quiescent channel remains near `2048 * 128 = 262144` for every gain setting. The BLE application payload is 246 bytes:

```text
A6 6A | version=02 | 76 x 24-bit sample words
```

Each 24-bit word is little-endian and contains:

```text
bits  0..18 : normalized sample value
bits 19..20 : ADC channel (0..3)
bits 21..23 : gain code (0..7, representing 1..128)
```

One notification contains 19 samples for each channel, or 76 sample words in total.

## Build and flash

1. Import this directory into STM32CubeIDE as an existing STM32 project, or open `BLE_VGY6.ioc` with STM32CubeMX/CubeIDE.
2. Build the `Debug` configuration.
3. Flash the resulting image with STM32CubeProgrammer through ST-LINK, or directly flash the release HEX in `firmware/`.

If GNU Make reports a directory encoding error on Windows, clone or import the project under a short ASCII-only path such as `C:\work\AC_BLE_Wave`.

## Validation status

The bundled release firmware has been compiled and hardware-tested with all four ADC channels, independent automatic gain changes, center-preserving normalization, BLE transmission, the existing STM32WB receiver firmware, and the four-channel desktop viewer.
