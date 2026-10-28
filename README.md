# N.E.E.B.L.E.S. Connect 1.0.1 — candidato Q08H/Q08I

Módulo integrado de acceso remoto en Qt 6.8.2 + Rust 1.98.1. Boss gobierna su instalación y ejecución mediante CUSTOM V2, Construction, las políticas de dependencias y RuntimeLease.

## Funciones implementadas a nivel de fuentes y candidato

- Dashboard y Quick Connect: acceso manual sin persistir secretos de conexión; sesiones independientes del estado de bloqueo de Secrets.
- My Connect: equipos y configuraciones almacenados mediante la API de estructuras de **NEEBLES Secrets 1.0.7**, no mediante archivos JSON de perfiles. Incluye credenciales SSH/VNC, opciones Sunshine/Moonlight y perfiles VS Code replicables, conforme al contrato de estructuras del módulo.
- SSH (libssh2 y terminal libvterm), VNC (LibVNCClient con topología ExtendedDesktopSize multimonitor) y superficies Qt declaradas. Sunshine/Moonlight y VS Code dependen de sus capacidades de conexión y configuración específicas; no declarar su aceptación funcional antes de probarla.
- Estado de Secrets `master_key`/`unblock` y gating mediante Boss. Un Connect ya instalado puede permanecer ejecutándose cuando Secrets se bloquea; las operaciones que precisan credenciales deben ajustarse al estado disponible.
- Supervisor Rust/Qt con ciclo de vida, IPC y parada gobernados por Boss.

Las bóvedas KDBX y la Master Key pertenecen exclusivamente a Secrets. Connect nunca debe guardar contraseñas en Settings, registros de depuración ni `vault_general`.

## Estado comprobado (28-10-2026)

- Q08D: DEB `libvncclient1_0.9.15+dfsg-1+deb13u2+neebles1_amd64.deb` incorporado como candidato al delta de Connect en CUSTOM V2. La versión Debian original se conserva.
- Q08E: Qt compilado en Esbirro con GCC 14, Qt 6.8.2 y cabecera RFB Q06; ejecutable Release instalado en DESTDIR temporal.
- Q08E-R4R2: cierre ELF físico de 89 objetos, sin `DT_NEEDED` irresueltos. Los dos `RUNPATH` absolutos del paquete libproxy fueron autenticados físicamente; NO se certificó aún su confinamiento efectivo en RuntimeLease.
- Q08F-R1: 13 pruebas estructurales, Rust fmt, tests y Release aprobados.
- Q08G: dos ejecutables candidatos incorporados al repositorio junto a `tooling/esbirro/connect-q08g-release-candidate.json`, con estado explícito `RELEASE_CANDIDATE_NOT_CERTIFIED`.
- Q08H: preflight técnico OK, sin commit, sin push y sin instalación real.

## Compilación reproducible y autoridad Esbirro

La receta oficial de Qt es `tooling/esbirro/build_connect_101.py`. Utiliza exclusivamente el sysroot y material autenticado de CUSTOM/Esbirro. El parche de extensión RFB queda versionado en `tooling/esbirro/patches/neebles-q06-extendeddesktopsize.patch`, junto al adaptador fuente y su documento de procedencia.

La fuente Debian exacta, con sus cuatro parches CVE, se prepara siguiendo el dossier `tooling/esbirro/README_Q08I_REPRODUCIBILIDAD.md`. El procedimiento no depende de las bibliotecas instaladas en el host, y nunca edita el sysroot productivo.

```bash
python3 -B tooling/esbirro/build_connect_101.py --preview
python3 -B tooling/esbirro/build_connect_101.py --build
python3 -B -m unittest discover -s tests -p 'test_*.py' -v
```

Un build exitoso **no equivale** a certificación. Para cualquier reconstrucción, repetir auditoría ELF física, comprobar Qt plugins y cargar mediante el Boss RuntimeLease real. Esbirro certifica material; Boss gobierna su ejecución; el host no es autoridad material.

## Secuencia de publicación pendiente

1. Revisar los cambios Connect, completar los artefactos reproducibles y realizar su commit/push.
2. Actualizar `fetch/checkout` en CUSTOM V2 con el SHA definitivo de ese commit; preservar Essential, el mundo `modules.rust-qt` y CUSTOM classic.
3. Publicar CUSTOM V2 con integridad y manifest correctos; después fijar el mismo commit Connect en el Registry de Boss y verificar la revisión CUSTOM que consumirá.
4. Instalar/actualizar exclusivamente con Boss, probar RuntimeLease, carga de Qt plugins y confinamiento de `libproxy`.
5. Probar de forma real Quick/My, Secrets lock/unlock, persistencia KDBX, SSH, VNC multimonitor y apagado limpio.

No editar `/opt`, servicios, bóvedas ni inventarios a mano. No marcar como publicada ni certificada una versión solamente por compilar.
