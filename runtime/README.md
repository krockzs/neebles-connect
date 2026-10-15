# Runtime Connect — implementación pendiente

El entrypoint productivo se llamará `runtime/neebles-connect` una vez construido,
certificado y colocado en el módulo con la ruta exacta acordada.

Se implementará en Rust 1.98.1 y deberá establecer **Module IPC autenticado** por
`/run/neebles/modules.sock` con frames JSON de longitud prefijada de 4 bytes big-endian;
registro con identidad de módulo y sesión, eventos, errores, cierre y unregister.
No se crea ningún binario de relleno, script host fallback ni sesión simulada.

La interfaz Qt 6.8.2 es un consumidor privado de la autoridad del runtime;
no debe abrir sockets administrativos de Boss directamente.
