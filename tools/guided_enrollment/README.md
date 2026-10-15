# Goodix animated fingertip setup

Run `python3 guided-enroll.py` in your graphical user session. Requires GTK4 Python bindings, Pycairo/GTK Cairo integration, `fprintd-enroll`, `fprintd-verify`, `pkexec` and the Goodix 5e0a driver. This version guides the **right index** only.

The app backs up the selected enrollment, takes five baseline checks, guides twelve accepted enrollment touches, then checks center, tip, left, right and base three times each. Before each of the three different-finger checks, switch to your **actual right middle** and tick its confirmation. All fifteen positive checks and all three rejections must pass before retaining the new enrollment. A failed comparison, cancellation or normal window closure after training restores the original.

The active selected enrollment is replaced temporarily during the comparison. Its original and the candidate stay in a root-only directory under `/var/backups/goodix-full-pad/SESSION/`. Other enrolled fingers are preserved. If the app or machine crashes during training, restore the original with the exact session ID shown in the saved session JSON:

```bash
pkexec --disable-internal-agent /usr/bin/python3 /absolute/path/protected-template.py restore SESSION right-index-finger
```

The backup helper requires the normal user's `PKEXEC_UID` and checks session, finger, owner and checksum. Keep the original backup after a successful comparison too. The app saves comparison results locally; raw fingerprint images and template bytes are never written into this output folder.

The drawing is a placement animation. Green means a guided impression was accepted, not that the area has been spatially measured. See [RESEARCH.md](RESEARCH.md) for source links, the native composite-template pathway, and the distinction between practical placement testing and literal full-image stitching.

For a refinement after a completed comparison restored the original, `--reuse-baseline-session SESSION` reuses its five baseline results. The protected helper first verifies that the current selected template has the same checksum as that restored session. A changed template requires a fresh baseline. Every refinement still performs its twelve enrollment touches and all eighteen validation/rejection checks.

Software checks:

```bash
python3 -m unittest test_coverage_policy -v
python3 guided-enroll.py --preview
```

The accompanying driver reports matches before finger release and clears the quick-retry guard after success. A failed attempt retains its release guard. Enrollment advances only when the native added-impression counter advances. No proprietary DLL, template or raw capture is distributed here.
