# Validation of the October 2026 adaptation

Hardware: one realme laptop, USB `27c6:5e0a`, firmware `GFUSB_GM168SEC_APP_10036`, Ubuntu 26.04.1, kernel 7.0.0-38, fprintd 1.94.5. Results were recorded on 15 October 2026. Other hardware and distributions have not been validated for these changes.

## Software

- The driver built successfully against the pinned libfprint fork.
- The portable upstream suite passed 348 executed checks, with eight skipped checks.
- The new native C tests passed with AddressSanitizer and UndefinedBehaviorSanitizer.
- 160 structured, randomized and boundary cases produced identical bytes and return values from the reference and optimized normalizers.
- On the test laptop, normalization measured 271.69 microseconds before and 41.73 microseconds after: 6.51 times faster for this CPU task, saving about 0.23 milliseconds per frame. This is not an end-to-end unlock speed ratio.
- Full Nix lifecycle/suspend harnesses and NixOS module evaluation were not run locally. The included workflow retains those required lanes for CI.

## Hardware batches

| Batch | Matches | Rejections | Client timeout/errors |
|---|---:|---:|---:|
| Previous working driver, right index | 1 | 5 | 0 |
| Initial performance candidate, original enrollment | 5 | 1 | 0 |
| Refreshed enrollment experiment, subsequently reverted | 4 | 8 | 0 |
| Original enrollment restored | 2 | 5 | 1 |
| Diagnostic normal presses | 4 | 0 | 0 |
| Diagnostic placement changes | 5 | 1 | 0 |
| Final build, right index | 5 | 1 | 0 |
| Final build, right middle against right-index enrollment | 0 | 4 | 0 |

Final successful touch-detection-to-native-match times were `132.332, 23.760, 24.008, 25.393, 24.626` milliseconds; median 24.626 ms. The cold first press matched frame three; the other successful presses matched frame one. These timings exclude daemon startup, finger waiting, PAM and desktop rendering.

These small sequential batches varied in placement and pressure. They do not establish a population accuracy improvement, a false-accept rate, or zero-miss reliability. Misses remain. An eight-frame experiment did not recover observed rejections and was removed from the final code. Thirty-two normalization variants also failed to recover a captured rejected burst, so the measured normalization settings were retained.

The detector test confirmed a 20-second hands-off phase without phantom contact, actual contact, a 20-second steady hold, and release. Raw diagnostic images were deleted after analysis. This publication contains aggregate results and source, with no participant fingerprint images, enrolled templates, private keys or personal system paths from the tested laptop.
