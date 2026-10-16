# Runtime NEEBLES Connect 1.0.0

Rust 1.98.1. El binario `runtime/neebles-connect --intent open` se ejecuta como
proceso gobernado por Boss Construction (`modules.runtime`, RootFS `rust-qt`).

Usa `/run/neebles/modules.sock` mediante el grant `boss.modules.ipc`, frames JSON
con prefijo big-endian de 4 bytes, `register` de módulo/sesión/protocolo 1,
`subscribe`, `ping/pong`, `shutdown/shutdown_ack` y `unregister` al cerrar
la ventana Qt. Boss autentica al proceso según la identidad del kernel.

El Rust supervisa `bin/neebles-connect-desktop` como proceso hijo; cerrar la
ventana o perder el canal IPC debe liberar la sesión. Esto solo está probado a
nivel de build y tests unitarios F7; RuntimeLease y Boss Live siguen pendientes.
La Qt no recibe permisos administrativos de Boss; no se declara Tray.
