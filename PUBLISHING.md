# Uploading to GitHub

This folder is the repository root. It includes complete driver source, upstream build/installation support, source tests, documentation, attribution and the license. You can use it as the contents of a new repository. To preserve upstream Git history instead, fork the upstream repository and copy the changed/new files identified in CODE_INDEX.md into your fork.

## New repository

Create an empty repository on GitHub, then run these commands from this source folder. Replace the remote URL with the URL of the repository you created:

```bash
git init
git add .
git commit -m "Add per-unit Goodix 5e0a calibration and faster normalization"
git branch -M main
git remote add origin https://github.com/YOUR_ACCOUNT/YOUR_REPOSITORY.git
git push -u origin main
```

Keep `LICENSE` and `ATTRIBUTION.md` in the repository. Use the included README as the repository introduction. The GitHub Actions workflow runs the software checks; it does not verify fingerprint hardware.

The runtime requires a separately supplied Goodix engine DLL and, where applicable, the user's own sensor key. The example calibration is opt-in and specific to a measured sensor. The README describes these requirements and the observed misses.

No GitHub repository has been created and nothing has been pushed as part of preparing this folder.
