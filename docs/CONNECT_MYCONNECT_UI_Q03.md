# NEEBLES Connect — My Connect UI Q03

Estado: fuentes locales del módulo, antes de build Esbirro y aceptación instalada.

## Flujo del usuario

1. Crear un equipo con nombre e IP/hostname obligatorios; usuario general opcional.
2. Un equipo recién creado no tiene SSH, VNC, VS Code ni Sunshine.
3. Mediante «+», añadir SSH/VNC/Sunshine como configuraciones opcionales únicas por equipo.
4. Para VS Code, «+» permite crear tantas instancias como sean necesarias, identificadas con ID estable y puerto único por equipo.
5. Los botones Conectar SSH/VNC solo aparecen si el protocolo está configurado. VS Code y Sunshine/Moonlight todavía no inician transporte; ofrecen únicamente interfaz de administración y subpestaña preparada.
6. La eliminación del equipo se deniega mientras haya referencias VS Code o sesiones abiertas; Secrets aplica además integridad referencial.

## Contratos y autoridad

- Dos esquemas físicos: `myconnect` y `vscode`. `myconnect` NO es un quinto protocolo.
- El único propietario KDBX es Secrets; Connect consume exclusivamente `structure.*` mediante el broker privado autenticado.
- `Quick Connect` no accede a la bóveda, usa su selector de 4 protocolos y preserva SSH/VNC aunque Secrets se bloquee.
- El desbloqueo permite carga de My sin reconectar automáticamente. El bloqueo cierra sesiones My y purga datos de equipos de la caché.
- Las escrituras se hacen con revisión optimista y se rechazan ante conflicto. Cambiar un protocolo preserva los otros campos existentes; ninguna actualización reemplaza la bóveda.
- `lifecycle.json`, Install/Update, CUSTOM, Boss, Secrets, `/opt`, Git remoto y bóvedas reales no se alteran en este incremento.

## Trabajo posterior

- Inicialización idempotente de la bóveda durante Install y Update oficial en el momento de integración.
- Layout Compact/Expanded automático, clipboard SSH/VNC, multimonitor VNC, idiomas y Governor.
- Pruebas y build certificado con Esbirro: únicamente al cierre de la release, según instrucción de desarrollo.
- Eliminar de forma excepcional la bóveda provisional de Connect es una operación externa y autorizada exclusivamente durante el primer update, nunca una acción del updater.
