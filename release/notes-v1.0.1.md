# NEEBLES Connect v1.0.1 — Secrets integration

- Saved SSH/VNC hosts are stored only in vault-neebles-connect.kdbx through Secrets 1.0.2 IPC.
- No local JSON profiles and no plaintext password persistence.
- My Connect sessions close on Secrets lock/unavailability, Quick tabs remain independent.
- Existing vault must already be unlocked and provisioned by the governed install-vault lifecycle.
- Qt must be rebuilt with Esbirro before commit/release; runtime metadata and installed package hashes must be certified.
- Existing profiles.dev.json is not imported or deleted automatically.
