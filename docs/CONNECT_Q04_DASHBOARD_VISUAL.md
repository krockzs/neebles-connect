# NEEBLES Connect — Q04 / Paquete visual acumulativo

Fuente local en `~/NEEBLES/neebles-connect`. Se aplica **después** de Q01, Q02 y Q03, sobre las mismas fuentes. No borra bóvedas, no actualiza servicios ni modifica `/opt`. No ejecuta build ni suites de tests, por decisión del usuario.

## Estructura de interfaz

- Una única barra izquierda, siempre con Dashboard / Quick Connect / My Connect; la opción «Crear» se accede desde My Connect, nunca desde Compact.
- La primera pestaña superior es administrativa y fija: Dashboard, Quick y My comparten ese espacio. Las pestañas de equipos aparecen automáticamente al leer My Connect desbloqueado, no tienen X y se pueden reordenar; el arrastre no mueve la primera pestaña.
- Las subpestañas de cada equipo corresponden a configuraciones realmente registradas. SSH y VNC son operativas; una subpestaña por registro VS Code; Sunshine una. Los motores de VS Code y Sunshine **no** se implementan.
- Compact comprime la columna izquierda al tener **conexión SSH/VNC seleccionada y autenticada**. Si se selecciona Dashboard/Quick/My o protocolo sin conexión, recupera ancho normal. El resto de las sesiones continúa en segundo plano.
- Stop solo actúa sobre la sesión activa seleccionada, y no borra su KDBX.
- Los iconos de protocolo se leen de `assets/neebles-connect-{ssh,vnc,vscode,sunshine-moonlight}-256.png` previamente copiados por el usuario al Ryzen.

## Dashboard y My Connect

- Dashboard protegido por bloqueo de Secrets; lista con scroll, separadores renombrables, colores, orden mediante flechas, indicadores verdes/rojos **solo** en SSH y VNC (sesión real), y accesos que seleccionan la subpestaña sin conectarla automáticamente.
- My Connect añade grupo opcional al formulario de creación/edición. Se guardan equipo/grupo/orden y separadores en la **bóveda de Connect**, mediante el broker `structure.*` con revisiones optimistas.
- La estructura `dashboard` es **metadato de organización cifrado** (un registro `primary`, campo JSON string `layout`), no otro protocolo. Conserva `myconnect` y `vscode` y los 4 tipos acordados de conexión.
- Sin grupos, los equipos figuran bajo «Sin grupo». Dos accesos al mismo equipo físico por Tailscales distintos siguen siendo IDs lógicos distintos.

## Pendientes deliberados para fases funcionales y QA final

- Copy SSH con selección y scrollback completo, Copy VNC limitado al monitor seleccionado y enumeración física real de monitores: **no declararlos operativos aún**. En este incremento los controles no certificados están deshabilitados. Stop sí actúa.
- La ordenación de pestañas superiores es funcional en la sesión Qt; aún no se guarda entre arranques. No confundirla con el orden cifrado de filas del Dashboard.
- Certificación Qt 6.8.2/Esbirro, QA de interfaces, transporte IPC, reversibilidad y validación en máquina física: todos pospuestos hasta terminar la implementación completa.
- No se han tocado Secrets 1.0.7, Boss 2.0.8 ni CUSTOM V2. Antes de instalar esta release se hará la preparación excepcional acordada de la bóveda Connect provisional. Este aplicador nunca ejecuta dicho proceso.
