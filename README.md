# N.E.E.B.L.E.S. Connect 1.0.0

Primer módulo de acceso remoto NEEBLES OS: Rust 1.98.1 y Qt 6.8.2, SSH/VNC.

**Estado:** candidato productivo para probar instalación gobernada. El éxito de
F7/F8.3 no constituye certificación de Boss Install/Open/Close/Uninstall.
No se ofrece todavía Tray, Launcher, inicio automático ni
integración de Secrets. El splash de 3 segundos y los assets originales se preservan.

## Autoridades y contratos

- `manifest.json` schema 4: ID `neebles-connect`, versión `1.0.0`, entrypoint `runtime/neebles-connect`.
- `lifecycle.json`: `governor.open` delega a `construction.step/open-runtime`.
- `manifest.uninstall`: `main: governor`, sin servicios extras, Settings a elección del usuario.
- `surfaces.json`: vacío; abrir desde la pestaña administrativa Modules de Boss.
- `runtime/neebles-connect`: proceso Rust que registra Module IPC y supervisa la ventana Qt hija.
- `bin/neebles-connect-desktop`: UI Qt de SSH/VNC sin conexión administrativa directa a Boss.
- CUSTOM V2: `modules.rust-qt`, exclusivamente RootFS `rust-qt` para SSH/VNC y Qt.
- Construction: `runtime/construction/neebles-connect.json` en CUSTOM, generado solo
  con el SHA inmutable del commit fuente completo.

Boss es dueño de Install, Update, Uninstall, Governor, Module IPC, MaterialBinding y RuntimeLease.
NO instalar bibliotecas con APT desde el módulo, ni reutilizar servicios legacy de Test Module.

## Publicación y aceptación

1. Publicar únicamente el source productivo y registrar SHA completo de Connect.
2. Fijar el SHA idéntico en `fetch` y `checkout` de CUSTOM Construction; publicar CUSTOM.
3. Actualizar exclusivamente `registry/modules.json` de Boss al SHA Connect publicado.
4. Instalar por Boss UI → Modules. Comprobar Open, cierre con X y Uninstall con
   preservación/eliminación de Settings, y registrar evidencias físicas.
5. Solo con estas pruebas acreditadas considerar el módulo instalado y funcional.

No se publica el histórico de tests M1–M6/F1–F8 ni archivos de Downloads.
Conservar código, iconos, hashes y baseline existente.

Referencias: Test Module README 1.2.12; Boss README 2.0.0; CUSTOM V2 README;
`portable/esbirro/README.md`. Los prototipos e investigaciones no son autoridad productiva.
