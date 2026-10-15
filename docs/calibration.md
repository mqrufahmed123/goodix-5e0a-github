# Per-unit calibration

The driver reads `/etc/libfprint/goodix-5e0a-calibration.conf` when the device is created. Without this file it retains upstream defaults. Restart fprintd after changing the file.

The example under `examples/` records one tested 27c6:5e0a unit running `GFUSB_GM168SEC_APP_10036`. It contains measured analog and image-processing values, not a fingerprint template or encryption key. Do not treat these values as universal settings. The installer does not apply this example automatically.

## Format

The file must contain an `[FDT]` section with six integer `Down` values and six integer `Up` values, separated by semicolons. Each pair must satisfy `1 <= Down < Up <= 254`. Values represent the high byte of the threshold word; the low byte is `0x80`.

`[FDT] DAC` is optional and contains four integers from 1 to 255. It replaces the down/up detector's first DAC bank and the down command's trailing bank. The release command's trailing bytes are preserved.

An optional `[Image]` section requires four `DAC` integers from 1 to 255. Its optional normalization settings are:

| Setting | Accepted values | Upstream default |
|---|---|---|
| `Window` | Odd integer from 3 to 31 | 3 |
| `Gain` | Finite number from 0.1 to 8.0 | 1.5 |
| `Midpoint` | Integer from 16 to 240 | 128 |

Each loader validates its entire section before applying it. An invalid section logs a warning and leaves that section's defaults in place. Validation is independent between the FDT and image sections. The image DAC is not inferred from FDT DAC, so a calibrated file should specify both consistently.

## Hardware validation

Obtain DAC values and detector thresholds from measurements of the actual unit. There is no automatic calibration wizard in this release. Test untouched behavior for at least 20 seconds, then touch, a steady 20-second hold, and release. Confirm no phantom contact or release timeout before enrollment. Test both an enrolled finger and a different finger against that enrollment.

Native quality is used to rank captured frames, not to establish identity. Authentication continues to use the Goodix Milan match decision. Configuration does not expose an acceptance-threshold override.

Remove the override to restore defaults:

```bash
sudo rm /etc/libfprint/goodix-5e0a-calibration.conf
sudo systemctl restart fprintd
```
