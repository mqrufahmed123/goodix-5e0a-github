# Engine dependency

Supply `GoodixEngineAdapter.dll` from the Windows fingerprint driver for your laptop. It is a runtime dependency and is not included in this source package.

For the shell installer, pass its location explicitly:

```bash
./install.sh --dll /absolute/path/to/GoodixEngineAdapter.dll
```

You can also place a local copy in this directory. Git ignores DLL files. On NixOS, set `services.fprintd.goodix.dllFile` to your own engine path, or configure manual lookup as described in the root README.
