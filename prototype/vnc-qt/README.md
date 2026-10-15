# NEEBLES Connect — VNC Qt prototype (development only)

**Repository location:** `neebles-connect/prototype/vnc-qt/`

This Qt 6 Widgets + LibVNCClient prototype is intentionally **not** an installed NEEBLES module yet. It provides a simple framebuffer, mouse, basic keyboard, host/port, VNC password prompt and manual connect/disconnect. It does not use KRDC, Remmina, TigerVNC Viewer, SSH or NoMachine. No passwords are stored or written to CLI args; the prototype password prompt is **not** a credential-vault integration.

Design boundaries:

- Source is owned by `neebles-connect`. The eventual C++ Qt UI is governed by a Rust runtime and module declarations; this standalone executable is solely a transport/UI proof-of-concept.
- Production build must use Esbirro's controlled Qt **6.8.2** dev sysroot. Host Qt headers/libraries are not a certification authority.
- CUSTOM V2 certifies the module's package/world/rootfs/material truth. Do not ship this prototype via `modules.runtime` without the exact world, complete approved multi-rootfs manifests, ELF/ABI audit, grants and Boss integration.
- Qt Widgets was chosen because the current Connect candidate set has Qt6Widgets but not Qt6Quick. This does not set the final UI technology in stone.
- The UI is single-threaded: `rfbInitClient` can block during initial connection, and `WaitForMessage` is polled from a Qt timer. For production, move I/O off the UI thread and implement reconnect and cancellation.
- Keyboard mapping is basic and not yet guaranteed for every keyboard layout/dead key. Pixels are rendered as little-endian RGB32 for the target architecture; verify negotiated channel masks before claiming accuracy.
- Only use on networks and remote computers you are authorized to access; connect via the existing Tailscale network for tests. Raw VNC may not itself be encrypted.

## Build plan (not a host-install instruction)

The controlled laboratory must provide the CMake package `Qt6Widgets` and the pkg-config module `libvncclient` (often from `libvncserver-dev`). The runtime candidate `libvncclient1` is **not** a substitute for compiler headers and linker assets. No apt command should be run against the host as a shortcut. Use the certified Esbirro build environment with a controlled `CMAKE_PREFIX_PATH`, `PKG_CONFIG_*` values and sysroot; stage with DESTDIR and audit output before publication.

Example *only inside an appropriately configured controlled environment*:

```bash
cmake -S prototype/vnc-qt -B build/vnc-qt -DCMAKE_BUILD_TYPE=Release
cmake --build build/vnc-qt -j
```

## Source tests still pending

- Build with the actual Esbirro Qt 6.8.2 + LibVNCClient headers/pkg-config
- Validate ELF needed libraries, RUNPATH, Qt symbol versions and no host leaks
- Verify RFB authentication, framebuffer, keyboard, mouse and reconnection against authorized OMEN/Acer
- Define Rust runtime/governance, Module IPC, Lifecycle, Surfaces, Settings, and separate Tray provider
- Certify CUSTOM V2 multi-rootfs and execute via Boss RuntimeLease with session/network grants
