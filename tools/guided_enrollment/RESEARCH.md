# What multi-region enrollment can do on this reader

This sensor provides 64×80-pixel patches. To recognize a tip-side, left-side or base-side contact later, enrollment must include useful ridge detail from that region. Repeating the center does not establish that coverage.

Microsoft's [enrollment-capture API](https://learn.microsoft.com/en-us/windows/win32/api/winbio/nf-winbio-winbioenrollcapture) captures samples into a template, reports bad captures including merge failure, and requests further samples when the engine needs more data. Its [engine update interface](https://learn.microsoft.com/en-us/windows/win32/api/winbio_adapter/nc-winbio_adapter-pibio_engine_update_enrollment_fn) adds feature sets to an enrollment object. These interfaces support multi-sample enrollment; they do not specify a universal stitching algorithm or prove that a Windows progress graphic measures all anatomical skin.

Windows also defines [extended enrollment status](https://learn.microsoft.com/en-us/windows/win32/secbiomet/winbio-extended-enrollment-status) with required sample counts for the center and four edges. Those fields can support regional guidance when an adapter implements them meaningfully. They are sample requirements, not a pixel-accurate anatomical map.

The local driver already calls the Goodix Milan exports `enrolStartEx`, `enrolAddImage`, `enrolGetTemplate` and `templatePack`. The upstream reverse-engineering notes identify a stitched-impression counter at context offset +10 and an enrollment-progress value at +12. The current bridge exposes both. The new driver rejects a touch when the native count has not advanced, rather than advancing solely on a zero return code. The engine's internals remain proprietary; no spatial registration coordinates or trustworthy anatomical coverage map are exposed by this bridge.

The default guided path uses twelve overlapping placements: center, tip twice, tip-right, right twice, base-right, base, base-left, left, tip-left, center. Repeated tip/right placements use a small angle change. This was the best of the three tested paths, though it still failed the acceptance policy. It preserves all nine guide regions and gives the right side a second view. A third experiment replaced the final center with a third tip-side view; it regressed another region and is not the default. Moving in small steps gives neighboring impressions shared ridge detail. This is a guidance heuristic, not a measured overlap guarantee. A user can still place the finger differently from the drawing, so acceptance is checked afterward by region.

Research on [dense fingerprint registration and mosaicking](https://arxiv.org/abs/2004.05972) aligns corresponding ridge detail while accounting for nonlinear skin distortion, then selects and combines image regions. That is substantially more than tiling images side by side. A reliable reconstructed image would need validated registration, overlap and distortion estimates; it cannot invent unobserved ridges. Feeding a manually enlarged mosaic into this Milan bridge would also violate its fixed 64×80 input geometry. This implementation therefore uses the engine's existing composite-template builder instead of constructing an unvalidated raster for authentication.

The animation's blue target marks a requested placement. Green marks an accepted guided impression. Neither is a live fingerprint image, a measured spatial map, or a 100% coverage claim. The engine's per-frame `overlap` quality byte is not used as a percentage of the whole finger.

The installed DLL also exports `GetTptOverlapRate`. Static disassembly of that exact local binary at export RVA `0x3b160` shows four argument-home writes followed by `mov eax, 0x64; ret`: it always returns 100 and does not read fingerprint content. It cannot supply real overlap or whole-finger coverage feedback in this build. No call through an assumed function signature was made.

The DLL's `WbioQueryEngineInterface` points to a version-3 interface at RVA `0x130400`. Following Microsoft's documented [engine-interface layout](https://learn.microsoft.com/en-us/windows/win32/api/winbio_adapter/ns-winbio_adapter-winbio_engine_interface), its extended-enrollment-status slot points to RVA `0x5f470`. On its enrollment-success path, that function copies GeneralSamples into Center and writes literal zero to TopEdge, BottomEdge, LeftEdge and RightEdge. It therefore provides no distinct regional feedback through those fields in this binary. This is a static finding about this adapter, not a claim that Windows itself cannot provide regional enrollment guidance. Relevant disassembly is saved locally; no speculative Windows pipeline invocation was added to fprintd.

## Local comparison results

Each comparison required 15/15 positive placement matches and three actual-right-middle rejections to retain the replacement. All three candidates were saved in protected backups and the original restored.

| Path | Center | Tip | Left | Right | Base | Total | Different finger rejected |
|---|---:|---:|---:|---:|---:|---:|---:|
| Original perimeter walk | 3/3 | 2/3 | 3/3 | 1/3 | 3/3 | 12/15 | 3/3 |
| Second right-side view | 3/3 | 1/3 | 3/3 | 3/3 | 3/3 | 13/15 | 3/3 |
| Third tip-side view | 3/3 | 1/3 | 0/3 | 3/3 | 3/3 | 10/15 | 3/3 |

The user confirmed that tip tests used the flat ridged pad near the fingertip. Every enrollment reached native impression counts 1 through 12. Thus the best of these small comparisons improved several practical placements but failed reliable tip recognition; adding a guided tip view alone did not solve that and regressed another region. No candidate satisfied the declared acceptance policy. The tests do not justify activating a replacement as fully accurate.

Verdict: **falsified** — these three fixed twelve-touch guide paths did not produce reliable recognition across all five tested regions. The single next experiment is an offline capture study comparing same-region tip enrollment/probe alignment and the Windows preprocessing pipeline before further enrollment changes. It should retain protected originals and avoid collecting another large live batch until the information gap is resolved.

Practical acceptance requires the center, tip-side pad, left, right and base-side pad to each match three times, followed by three rejections of the user's explicitly confirmed actual right middle finger. These few checks can identify a bad enrollment or obvious regression; they cannot establish a population false-accept rate, liveness detection, perfect accuracy, or recognition of every possible extreme edge contact.

Unlock behavior is independent of enrollment motions. Verification reports a match before any finger-up wait. A successful authentication now clears the quick-retry release guard. A failed authentication retains the existing short retry-release behavior. Enrollment itself still needs lifted, repositioned impressions to collect multiple regions.
