# NEEBLES Connect — Q07 resiliencia / pendiente obligatorio antes de release

**Prioridad:** después de Q06; antes de QA final, commit, pin CUSTOM/Boss y actualización instalada.
**Motivo:** el usuario ha observado bloqueos al reiniciar equipos remotos y al caer conexiones.

## Obligatorio

1. **VNC sin red bloqueante en GUI:** mover conexión inicial, autenticación, `HandleRFBServerMessage`, recepción y cierre a una autoridad/hilo propio; notificar a Qt por colas, limitar buffers y tiempo de espera. Ni siquiera destruir el panel desde GUI podrá esperar indefinidamente por una conexión caída.
2. **SSH cancelación/acotación:** `getaddrinfo` puede bloquear, las fases handshake/libssh2 y `worker_.join()` pueden congelar cierre. Introducir cancelación y plazos globales de conexión y liberación; no almacenar punteros de UI para un worker tras destrucción.
3. **Secrets IPC:** `QLocalSocket::waitForReadyRead(35000)` dentro de GUI es inaceptable. Cambiar a async, con deadlines y correlación de solicitudes. Preservar gating `master_key`/`unblock`; con vault bloqueada solo cierran My, no Quick. No generar reintentos que dupliquen escrituras.
4. **Conectividad/estado:** el cierre abrupto cambia solo el indicador SSH/VNC afectado a rojo; no se cierran otras pestañas, se permite reconectar y compact vuelve automáticamente a normal en la pestaña inactiva.
5. **Lifecycle:** cierre ordenado con Boss 2.0.8, sin bloquear Qt, sin procesos abandonados ni operaciones sobre bóvedas ajenas.
6. **QA final Esbirro:** casos de reboot remoto, corte de red/Tailscale, timeout RFB, EOF SSH, cancelación en autenticación, bloqueo Secrets, cambio de monitor RFB, cierre ventana, reinicio y múltiples conexiones simultáneas.

No asignar verde a Q07 hasta ejecutar los gates finales. No cambiar Secrets ni Boss salvo que un defecto probado del contrato de ellos lo exija.

## Q07A — Primer incremento aplicado a fuentes (sin QA)

- Sondeo `vault.exists` periódico ejecutado fuera del hilo Qt, con un solo
  sondeo en vuelo y plazo global de 2500 ms. Solo el hilo gráfico procesa
  los resultados y gobierna el estado My/Quick.
- `secretsRpc` acotado mediante un cronómetro monotónico global: ninguna
  respuesta fragmentada puede renovar indefinidamente su espera.
- `ensureVaultReady` conserva su verificación de lectura en el punto de uso
  con presupuesto de 1500 ms (en vez de hasta 35 segundos), conservando
  el control autoritativo del broker para cualquier acceso a bóveda.
- Se evita reconstruir Dashboard, listas y pestañas cada segundo cuando el
  estado permanece bloqueado o desconectado.

**Q07 sigue PENDIENTE:** las operaciones de estructura KDBX disparadas desde
acciones de usuario aún son síncronas; falta la migración de VNC a un worker
que no bloquee Qt, la cancelación SSH/DNS y la revisión final de lifecycle.
Este incremento NO soluciona todavía todos los congelamientos descritos.

## Q07B — Aislamiento de SSH / cierre no bloqueante (solo fuentes)

- Cada conexión SSH tiene un `SessionState` compartido independiente del
  `QWidget`, que contiene socket, estado, tamaño PTY y buffers acotados.
- `disconnectNow()` señala cancelación y aplica `shutdown()` al socket, pero
  **nunca espera `join()` desde Qt**. El worker detached es dueño de su estado
  y libera su socket al terminar. El bloqueo de DNS ya no congela la GUI.
- Las notificaciones Qt se distribuyen por cola y se descartan si la pestaña
  desapareció o pertenece a una sesión anterior (previene uso tras liberar).
- Se habilita TCP keepalive acotado y se atienden POLLERR/POLLHUP/POLLNVAL
  y errores fatales de lectura para detectar desconexiones remotas.
- Los buffers de salida SSH hacia Qt están limitados a 4 MiB y las escrituras
  locales pendientes a 256 entradas; evitar tormentas de eventos.
- Revisiones de huella de servidor requieren aceptación explícita, son
  cancelables y tienen vencimiento de 3 minutos. Nunca se autoaceptan cambios.
- Al reconectar, nueva sesión y nueva identidad, sin interferir con otro host.

**Pendiente de revisión/QA integral:** comprobar la liberación del worker ante
resolver DNS no cancelable y casos límite de apagado del proceso; migración de
red VNC a worker; IPC de las acciones structure.*; lifecycle gobernado.
Sin compilación ni tests de runtime en esta fase.

## Q07C — VNC de red aislado de Qt (solo fuentes, pendiente QA)

- LibVNCClient y el socket RFB pasan a un worker por sesión; la conexión inicial,
  recepción, escritura de eventos, autenticación y cleanup no ejecutan trabajo
  de red en el hilo gráfico.
- El worker copia el framebuffer RGB32 a un QImage independiente como máximo
  cada 40 ms y publica **solamente el último frame**; la GUI lo consulta con
  un QTimer de 40 ms. No se acumulan señales ni colas de imágenes.
- Las geometrías de `ExtendedDesktopSize` se reciben y verifican en el worker,
  se copian a un snapshot y conservan los IDs RFB de Q06. La selección del
  monitor, sus coordenadas remotas, el fit y Copy se preservan en Qt.
- Teclado, mouse y rueda se encolan (1024 entradas máximo, coalescing para
  movimientos de mouse); únicamente el worker invoca SendKeyEvent y
  SendPointerEvent. Desbordamientos de eventos no eliminan silenciosamente
  una liberación de teclas: se cancela la sesión.
- Stop, destrucción del QWidget y reconexión **no esperan join**: cancelación
  + shutdown del socket protegido por mutex. Los workers conservan solo su
  estado, nunca punteros QWidget; una sesión antigua no modifica la nueva.
- Se añade heartbeat RFB de 1×1 píxel (sin SetDesktopSize) tras inactividad,
  para detectar caídas silenciosas; vencimiento tras 20 s sin mensajes RFB.
  Connect no modifica la resolución del servidor remoto.
- `connectTimeout` y `readTimeout` de LibVNCClient se fijan en 5 segundos,
  pero la resolución DNS es responsabilidad del worker y todavía requiere
  evaluación de límites del sistema durante QA.

**Limitaciones y gates:** sigue pendiente certificar LibVNCClient Q06 mediante
CUSTOM V2/Esbirro, verificar la compilación completa y probar estos escenarios:
reboot remoto, desconexión Tailscale, VNCAuth, multimonitor, imágenes grandes,
varias sesiones simultáneas y cierre rápido con worker autenticándose.
**Q07D pendiente:** hacer no bloqueantes las operaciones interactivas de
Secrets (`structure.*` y vault) y validar lifecycle Boss. No declarar Q07
certificado antes de QA integral.

## Q07D1 — Activación de bóveda sin bloqueo gráfico (solo fuentes)

- `vault.exists` continúa asíncrono con el sondeo Q07A.
- La entrada `Secrets unlocked` ya no ejecuta `ensureSchemas`, `structure.list`
  ni lectura `dashboard` en el hilo Qt: el worker toma una instantánea que
  solo incluye atributos no secretos de perfiles y recuentos VS Code.
- La instantánea se valida y aplica en GUI, preservando validación del layout,
  agrupaciones, colores, nombres y el modo *fail closed*. El worker revisa
  nuevamente `vault.exists` antes de devolver los datos.
- `ensureVaultReady()` ya no hace I/O: devuelve el estado observado más
  recientemente. El broker Secrets **siempre** conserva la autorización real
  de cada `structure.*`; no se confía en este valor para autorizar escrituras.
- La acción `vault.request` se despacha una sola vez mediante QThreadPool;
  el resultado se procesa en GUI. No reintentar si la respuesta es ambigua.
- Los eventos `QPointer` se descartan cuando el MainWindow ya fue destruido;
  ningún worker guarda punteros de `QWidget` de larga vida.

**Aún pendiente Q07D2:** convertir las operaciones manuales de
`saveProfile`, `deleteProfile`, `configureFixedProtocol`, `manageVsCode`,
`saveDashboardLayout` y la obtención de credenciales de `openProfile` en
acciones asíncronas correlacionadas/serializadas. Estos caminos todavía
invocan `Store` en Qt y **Q07 NO está cerrado**. La validación del lifecycle
Rust/Boss 2.0.8 y toda la QA integral continúan pendientes.

## Q07D2A — Edición de equipos, layout y tabs sin I/O Qt (fuentes, sin QA)

- El UI no ejecuta `structure.list(vscode)` al construir pestañas: usa
  únicamente nombres e IDs incluidos en el snapshot Q07D1 (sin contraseñas).
- `saveProfile`, `deleteProfile` y las escrituras de layout de Dashboard
  se despachan a un worker, **una sola operación manual en vuelo**.
- El worker usa GET + CAS (`revision`) con Secrets; no reintenta ninguna
  escritura tras timeout. Las dos escrituras equipo + layout no son una
  transacción atómica: si falla layout, se avisa del éxito parcial y no
  se repite la escritura de equipo.
- Un cambio de estado de bóveda invalida callbacks tardíos con `vaultEpoch_`.
  Los widgets solo son manipulados en el hilo Qt.
- Si fracasa la escritura de layout tras una edición optimista, se revierte
  al último snapshot persistido y se notifica explícitamente.

**Q07D2 pendiente:** configurar/eliminar protocolos individuales, CRUD de
instancias VS Code y obtención de credenciales todavía invocan Store desde Qt;
`loadProfiles` legado también requiere retirada. No declarar todos los accesos
asíncronos. Lifecycle Boss y certificación Esbirro todavía pendientes.

## Q07D2B — Protocolos y credenciales en workers (solo fuentes)

- Carga de campos de SSH, VNC y Sunshine fuera de Qt; solo se exponen campos de edición no secretos. Contraseñas previas nunca se precargan en los diálogos.
- Mutaciones de protocolos e instancias VS Code con `structure.put/delete` y CAS por revisión; sin retry ante timeout y sin perdida de claves de otros protocolos.
- Listado de VS Code 0..N sin contraseñas hacia GUI; creación, edición y eliminación vía worker, una operación en vuelo.
- Botones Conectar de los paneles y My Connect usan una ruta única para recuperar una credencial bajo Secrets con worker. Quick sigue independiente; no hay lectura KDBX desde `QWidget`.
- Q07D2B sigue sin QA ni compilación; lifecycle Boss y certificación de la extensión LibVNCClient permanecen pendientes.

## Q07E — Cierre gobernado por Boss (fuentes, sin QA)

- El runtime Rust conserva el protocolo-v1 de `/run/neebles/modules.sock`:
  registro, suscripciones, ping/pong, unregister y shutdown_ack autenticados.
  No incorpora llamadas IPC a Secrets a traves de Boss sin el grant del
  proveedor; las operaciones KDBX permanecen en su socket privado autorizado.
- Cuando Boss envia `shutdown`, Rust primero solicita SIGTERM a la interfaz
  Qt, espera como maximo 3 s y fuerza el cierre si fuera necesario. Solo
  entonces emite `shutdown_ack`; no confirma la terminacion de Qt antes de
  haberla observado o forzado.
- Qt procesa SIGTERM/SIGINT mediante un self-pipe y QSocketNotifier. El
  manejador POSIX no manipula QWidget, sockets ni hilos de sesion.
- Se limita a 3 s la escritura sobre modules.sock, sin escrituras IPC
  indefinidas. Un cierre no exitoso de Qt no se reporta como Open exitoso.
- `PR_SET_PDEATHSIG` comprueba el PID padre tras instalar la señal, cerrando
  la carrera fork/exec si muere el runtime antes de ejecutar Qt.

**Aun pendiente:** QA integral de cierres durante splash, conexion y worker
activo, TERM→KILL, reinicio de Boss, cortes de IPC y revisiones de seguridad.
La version publicada, manifiestos de release y pin de CUSTOM no se modifican
por este parche. Q07E no declara Q07 certificado.
