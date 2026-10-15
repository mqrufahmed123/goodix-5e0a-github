# Goodix 27c6:5e0a Linux fingerprint driver — calibration update

A source distribution of the Goodix 5e0a libfprint driver with per-unit calibration and faster image normalization. Based on [jitendradara12/goodix-5e0a](https://github.com/jitendradara12/goodix-5e0a), commit `516b2e9e9efdb75c73d544208e646c64b5460dc4`. See [ATTRIBUTION.md](ATTRIBUTION.md) and [LICENSE](LICENSE).

This adaptation was tested on one realme laptop running Ubuntu 26.04.1 with sensor firmware `GFUSB_GM168SEC_APP_10036`. The final batch matched five of six right-index presses, with a 24.626 ms median from touch detection to native match among successful presses. Four different-finger tests were rejected. **Misses remain; this is not a zero-miss or broadly validated release.** [Full validation](docs/validation.md).

## Changes

- Reject non-contact replies with zero active detector channels.
- Load optional per-unit detector thresholds, factory DAC values and image-normalization settings.
- Match each suitable frame of a four-frame burst and preserve the exact frame that matched.
- Replace nested local-window summation with an integral image. Output was byte-identical in 160 tests; measured normalization CPU time improved 6.51 times, saving about 0.23 ms per frame.
- Add native configuration and equivalence tests, including sanitizer runs.
- Park a healthy TLS connection after a completed verify/identify scan, allowing consecutive attempts within one open claim to use the existing health-checked reuse path. Errors, cancellation during capture, expiry and suspend retain their recovery paths. A six-attempt follow-up on the same reader matched 6/6 with zero new handshakes; median request-to-healthy-reuse was 5.919 ms versus one historical 307.849 ms reconnect. Median contact-to-client-result was 28.544 ms. These figures exclude desktop unlock animation and do not establish Windows performance parity.
- Report successful matches while the finger is still down and clear the quick-retry release guard after success. Failed attempts retain the existing release guard. The native matching rules are unchanged.
- Advance enrollment only when Milan reports a new added impression. The optional [animated setup app](tools/guided_enrollment/README.md) guides overlapping positions and tests the center, tip, sides and base before retaining a replacement enrollment. Its [research notes](tools/guided_enrollment/RESEARCH.md) explain composite templates and the limits of a placement animation.

The Goodix Milan matching rules and acceptance threshold are unchanged. This distribution includes complete runtime source, integration/build files, installation scripts, upstream tests and additional tests. It contains no prebuilt library, proprietary engine DLL, private device key or fingerprint template. [Complete code index](CODE_INDEX.md).

The animated enrollment tool remains experimental: three local guide paths matched 12/15, 13/15 and 10/15 placement checks. Tip recognition remained weak; all candidates failed the declared acceptance policy and the original enrollment was restored. Each candidate rejected three confirmed different-finger probes. The animation does not measure anatomical coverage, and the supplied DLL's overlap export returns a constant 100. See the tool's research notes for details.

## Runtime requirements

The new experimental [observed-area tracker](tools/observed_coverage/README.md)
aligns accepted capture texture and records distinct observed area. Repeated
captures that register to the same area do not grow the map. Uncertain captures
stay unmapped; this does not identify anatomical regions or establish complete
finger coverage. Optional patches expose the map through a claim-protected
fprintd method and display it in GNOME Settings. They target specific daemon
and Settings source versions and are separate from the normal driver installer.

- x86-64 Linux and a supported `27c6:5e0a` sensor; the changes were hardware-tested on the unit described above.
- The matching `GoodixEngineAdapter.dll` from the Windows fingerprint driver for your laptop. This source-only package does not include that dependency. Supply its path to the installer.
- A compatible sensor PSK. Units with a different key need their own `/etc/libfprint/goodix-5e0a.psk`; see [key extraction](docs/psk-extraction.md). Do not put your private key in this repository.

## Fresh installation

Run the installer as your normal user. It builds from source and requests administrator access for dependencies and the private installation transaction:

```bash
./install.sh --dll /absolute/path/to/GoodixEngineAdapter.dll
sudo systemctl restart fprintd
fprintd-enroll
fprintd-verify
```

`./install.sh --help` lists the build-only, dependency and uninstall options. It refuses to overwrite an existing private installation. For SELinux systems, follow [the policy instructions](packaging/selinux/README.md) or run `./install.sh --integrate`. Desktop/PAM setup is separate: on Ubuntu/Debian install `libpam-fprintd` and enable fingerprint authentication with `sudo pam-auth-update`; GNOME also provides fingerprint settings. Password authentication remains available.

An unprivileged staging build can be created with:

```bash
./install.sh --dll /absolute/path/to/GoodixEngineAdapter.dll --build-only /absolute/new-staging-directory
```

The private installation lives under `/opt/goodix-libfprint`; it does not replace the distribution's shared libfprint. Uninstall using `./install.sh --uninstall`. Enrolled prints are preserved.

## Calibration

No calibration override is installed automatically. Without an override the driver uses upstream defaults. The example in [examples/realme-book-app10036.calibration.conf.example](examples/realme-book-app10036.calibration.conf.example) records one sensor's measured settings. Other units require their own measurements; do not apply that example as a universal preset.

See [docs/calibration.md](docs/calibration.md) for the configuration format and hardware validation procedure. Automatic per-unit calibration is not implemented in this release.

## Software checks

The new native tests need a C compiler, pkg-config and GLib development headers. They do not access the sensor:

```bash
bash scripts/test-unit-calibration.sh
GOODIX_SANITIZERS=1 bash scripts/test-unit-calibration.sh
bash scripts/test-coverage.sh
```

Run the portable upstream checks when Nix is unavailable:

```bash
GOODIX_NATIVE_TESTS=skip GOODIX_SUSPEND_TESTS=skip bash tests/run_all_tests.sh
```

For the complete upstream lane with Nix and a configured nixpkgs path:

```bash
bash tests/run_all_tests.sh
```

The full lane builds native lifecycle and suspend harnesses. The GitHub Actions workflow retains these lanes and adds the calibration/normalization C checks. Portable and C checks passed locally, along with locally compiled native lifecycle/suspend harnesses for the follow-up changes. The packaged Nix lane has not been run locally for these follow-ups; see `SOURCE_MANIFEST.json` for exact validation scope.

## NixOS

The included flake and module build the same driver source. Point your flake input at your fork, import `goodix.nixosModules.default`, and set your own engine path:

```nix
services.fprintd.goodix.dllFile = /absolute/path/to/GoodixEngineAdapter.dll;
```

This source-only package defaults `dllFile` to `null`, which leaves manual engine lookup enabled. An explicit DLL path enters the Nix store. The NixOS support is inherited from upstream and has not been hardware-tested for this adaptation.

## Publishing this source

Commit the contents of this folder as a new repository, or apply the runtime changes to an upstream fork. `CODE_INDEX.md` identifies the files changed in this adaptation; `SOURCE_MANIFEST.json` records provenance and file hashes. Keep the license and attribution. The ignore rules exclude private key files, local engine DLLs, sensor dumps and build outputs.
