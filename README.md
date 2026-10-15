# N.E.E.B.L.E.S. Connect

**Versión objetivo:** 1.0.0. **Estado actual:** preparación de contratos; **NO publicable ni instalable todavía**.

## Identidad

Este repositorio contiene la implementación progresiva del módulo Connect para NEEBLES OS.
El producto final tendrá backend Rust, UI Qt 6.8.2, SSH/VNC por nodo y Tray gestionado
mediante el Boss Tray Manager. Los detalles de UI/tray permanecen gobernados por el
Documento Maestro del 12-10-2026 y su complemento técnico del 15-10-2026.

La IP Tailscale del laboratorio actual NO está fijada en el código ni en manifiestos.

## Fuente de verdad

- `neebles-boss/README.md` — Governor, Module IPC, Settings, Surface, Registry, MaterialBinding, RuntimeLease.
- `neebles-custom/README_CUSTOM_V2.md` — Essential/delta, RootFS/world, Construction.
- `neebles-custom/portable/esbirro/README.md` — material controlado y certificación.
- `neebles-test-module/README.md` — tutorial vigente 1.2.12, no implementar Python/Tk por imitación.
- `prototype/vnc-qt/` — prototipo real de laboratorio, **no** runtime publicado.
- `integration/` — investigaciones y evidencias; no fuente de permisos productivos.

## Estado de contratos

- `manifest.json` schema 4, ID estable y versión objetivo.
- `lifecycle.json` contiene solamente los transiciones declarativas base. `governor.open` todavía **NO** existe.
- `surfaces.json` no declara acciones que no funcionen.
- `contracts/commands.json` contiene el documento de preparación sin endpoints ni referencia activa en manifest.
- `settings/default.json` declara idioma y preferencias de Tray, pero la presencia del valor no arranca servicios.
- Diccionarios `en_US` y `es_CL` listos para integración posterior en Qt/Rust; no acreditan UI traducida.
- No se declara `manifest.tray` ni Notifications mientras el proveedor y el runtime no existan.
- `manifest.entrypoint` especifica el ejecutable objetivo `runtime/neebles-connect` **aún no construido**.

**NO añadir al Boss Registry ni intentar Boss Install en este estado:** faltan ejecutable,
Module IPC autenticado, Construction, RootFS, grants, pruebas y validación real.

## Orden de siguientes gates

1. Auditar estado local y leer trabajo NO trackeado `integration/custom-v2/connect-runtime-reconciliation.proposed.json`
   y `integration/esbirro/`; preservar `assets/` y `prototype/`.
2. Cerrar límite Rust↔Qt y semántica de X / ventana y Tray. Conservar baseline VNC V4.
3. Implementar runtime Rust Module IPC + Qt integrado, Open/Close y puertos configurables.
4. Certificar mundos y RootFS en CUSTOM V2/Esbirro; declarar Construction `open-runtime`
   y `tray-provider` únicamente cuando existan implementaciones reales.
5. Agregar surfaces/commands/manifest.tray según funcionalidad acreditada.
6. Publicar source SHA, luego CUSTOM V2 exacto, luego Boss Registry y probar instalación real.

## Assets

Los archivos gráficos viven en `assets/` y deben mantenerse exactamente como los preservó el usuario:
`neebles-connect-icon.png`, `neebles-connect-tray-icon.png`, `neebles-connect-logo.png`.
El logo se reserva para splash de 3 segundos. No insertar nuevas imágenes en este paquete.
