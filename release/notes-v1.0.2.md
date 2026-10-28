# N.E.E.B.L.E.S. Connect 1.0.2 — release candidate

NEEBLES Connect 1.0.2 consolida el desarrollo Q01–Q08: Quick Connect sin persistencia de contraseñas, My Connect mediante estructuras cifradas de Secrets 1.0.7, VNC ExtendedDesktopSize multimonitor y mejoras de cancelación SSH/VNC, asincronía de Secrets y cierre de procesos gobernado.

## Esquemas de la bóveda de Connect

- `myconnect`: nombre, IP, usuario, credenciales y ajustes SSH, VNC y Sunshine.
- `vscode`: cero o más instancias por equipo, con nombre, puerto y credencial, validadas por Secrets.
- `dashboard`: diseño persistido mediante Secrets.

**Transición excepcional:** se ha acordado retirar exclusivamente la bóveda anterior `vault-neebles-connect.kdbx` y crear otra limpia bajo Secrets. Esta release **no borra bóvedas**: esa operación requiere una fase explícita con procesos inactivos, verificación de ruta/propietario y control independiente. `vault-general.kdbx`, Master Key y bóvedas ajenas se preservan.

## Certificación pendiente

Qt 6.8.2/GCC 14 Q08E y cierre ELF físico Q08E-R4R2 están aprobados. Rust 1.0.2 requiere QA y compilación Release nuevas. La instalación, los plugins Qt, la resolución de RUNPATH de libproxy y la aceptación bajo Boss RuntimeLease aún no han sido demostrados en ejecución. El lanzamiento y la integración con Secrets/SSH/VNC siguen sujetos a esas pruebas. No cambiar CUSTOM V2 o Boss antes de publicar el commit 1.0.2.
