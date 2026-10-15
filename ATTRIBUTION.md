# Origin and changes

This is a derivative of [jitendradara12/goodix-5e0a](https://github.com/jitendradara12/goodix-5e0a), based on commit `516b2e9e9efdb75c73d544208e646c64b5460dc4`.

The USB/TLS transport, Goodix Milan PE loader, integration patch, installation and packaging machinery, most tests, and archived experimental source come from that project. Their copyright notices and the LGPL-2.1-or-later license are preserved.

The October 2026 adaptation adds per-unit contact/image configuration, rejects phantom zero-channel contact, matches every suitable burst frame while preserving the matching frame, and accelerates local-mean normalization without changing its output. New native tests cover configuration validation and normalization equivalence. See CODE_INDEX.md for the complete source inventory.

This source package excludes the proprietary `GoodixEngineAdapter.dll`. Users supply their engine separately. The public upstream's sample key and deterministic cryptographic test vectors remain in the upstream source; no private key or fingerprint data from the tested laptop is included.

The measured example configuration is not automatically installed. NixOS defaults to manual DLL lookup in this source-only distribution; users can set `services.fprintd.goodix.dllFile` explicitly.
