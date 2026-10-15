# Registered observed-area tracking

The driver now observes Milan-accepted 64×80 enrollment captures and estimates
their relative poses using masked correlation, rotation/translation search,
peak ambiguity, spatial support, reverse consistency, and overlapping-anchor
checks. Only confidently aligned textured support can grow a bounded map.
Repeats, noisy/blank frames, disconnected texture, and ambiguous ridge patterns
do not gain mapped area. Source patches stay in RAM during enrollment and are
cleared when it ends or fails.

This is an experimental rigid registration observer. It does not classify
anatomical tip/center/sides, know the unseen finger boundary, or report a
whole-finger percentage. Skin deformation can make legitimate captures
unplaceable. Extension is limited to two registration hops to bound drift.
Native Milan template construction, recognition thresholds, and twelve-capture
enrollment remain in use. Limited measured coverage does not by itself prevent
native enrollment completion.

Run the standalone synthetic tests without hardware:

```sh
bash scripts/test-coverage.sh
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
LDFLAGS='-fsanitize=address,undefined' bash scripts/test-coverage.sh
```

The actual-driver lifecycle harness also verifies native acceptance gates,
coarse-only snapshots, session reset, and cleanup across cancellation, suspend,
errors, close, and finalize. Synthetic fixtures contain no personal biometrics.
Software tests do not validate recognition accuracy or real-finger deformation.

## Optional native Settings display

`fprintd-1.94.5-coverage.patch` applies to the distribution-patched Ubuntu
`fprintd 1.94.5-4` source. It adds
`net.reactivated.Fprint.Device.GetEnrollmentCoverage() → a{sv}`. Only the bus
connection holding the device claim can read it. There are no image/template
payloads, public coverage properties, or broadcasts. The dictionary is empty
before native enrollment begins, then includes versioned session/frame counters,
coarse occupancy, relative pose, and confidence. Normal upstream clients remain
compatible.

`gnome-settings-50.3-coverage.patch` applies to Ubuntu
`gnome-control-center 1:50.3-0ubuntu0.2` after its distribution patches. It embeds
placement guidance and a separate observed-area map in the existing enrollment
dialog. It handles fprintd's initial duplicate-check stage separately from
twelve enrollment captures. Stale callbacks/maps are rejected and unavailable
coverage is stated explicitly.

Exact source archives are available from Ubuntu:

- [fprintd source descriptor](https://archive.ubuntu.com/ubuntu/pool/main/f/fprintd/fprintd_1.94.5-4.dsc)
- [Settings source descriptor](https://archive.ubuntu.com/ubuntu/pool/main/g/gnome-control-center/gnome-control-center_50.3-0ubuntu0.2.dsc)

Extract the corresponding source with its distribution patches, then apply the
appropriate patch using `patch -p1`. Build against matching development
libraries and schemas. These optional application changes are not installed by
the driver installer; deployment is distribution-specific. Prefer private
executables and version-guarded launchers, retaining distribution executables.

`build-settings-test.py /absolute/path/to/configured/gnome-control-center-50.3`
uses that tree's compiler/linker flags to build the accompanying real-dialog
harness. It requires a graphical GTK4 session and mocks device operations.
The twelve tests include metadata validation, repeats, session isolation,
unavailable methods, empty duplicate-check snapshots, and final-frame fetching.

`test-coverage-api.py --source /absolute/fprintd-source --daemon-dir
/absolute/fprintd-build/src --virtual-build /absolute/virtual-only-libfprint-build -v`
uses upstream fprintd test dependencies and an isolated fake bus. It refuses
the installed daemon or a libfprint test build containing real hardware drivers.
The adjacent preload helper supplies only synthetic metadata and must never be
installed. Fourteen tests cover access ownership and pending authorization.

For registration background and the limits of rigid alignment, see
[Dense Registration and Mosaicking of Fingerprints](https://arxiv.org/abs/2004.05972).
