# NEEBLES Connect v1.0.1 — candidato de publicación Q08H/Q08I

Este documento recoge el contrato integrado vigente de **NEEBLES Secrets 1.0.7**.

## Funciones incluidas en el candidato

- Acceso remoto SSH/VNC integrado mediante Qt 6.8.2, libssh2, libvterm y LibVNCClient.
- Extensión Q06 de `ExtendedDesktopSize` para describir monitores de forma separada.
- Quick Connect independiente de la bóveda y My Connect respaldado por la API de estructuras de Secrets; sin perfiles en texto plano.
- Manejo de estado bloqueado/desbloqueado de Secrets, solicitudes IPC asíncronas, cierres de sesiones, protección de UI y ciclo de vida gobernado por Boss.
- Configuraciones extensibles SSH/VNC/Sunshine-Moonlight y entradas VS Code replicables, con validación funcional real pendiente.

## Evidencia técnica

Compilación Qt y Rust satisfactoria; 13 tests estructurales y Rust fmt/test/Release OK; auditoría ELF física Q08E-R4R2 de 89 objetos y 0 `DT_NEEDED` pendientes. La LibVNCClient nueva conserva los símbolos previos y exporta la API de topología Q06.

## Gate abierto

**No publicado, no instalado, no certificado para RuntimeLease.** Deben actualizarse pins de CUSTOM Construction y Boss Registry tras el commit de Connect, certificar el material vinculado y ejecutar la aceptación real bajo Boss. La resolución de dos `RUNPATH` absolutos de libproxy es físicamente auténtica, pero debe validarse dentro de la frontera real del sistema.
