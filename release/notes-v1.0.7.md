# NEEBLES Connect 1.0.7 — candidato C64

Estado: fuentes de desarrollo, **no publicado**, no instalado, no certificado en RuntimeLease.

## Alcance

- VNC: expandir el framebuffer al area completa del panel, sin KeepAspectRatio; el calculo del puntero conserva escalas independientes X/Y y offset RFB de monitor.
- Interfaz: logo oficial de Connect ampliado al encabezado del sidebar expandido (217 x 180 px logicos).
- No cambiar recursos PNG, formato de datos, secretos ni interfaces de IPC.

## Restricciones

- Lifecycle heredado de 1.0.5/1.0.6: update.operations = {}. No ejecutar install-vault en actualizacion.
- Qt 6.8.2 + GCC 14 controlados por Esbirro; binario de DESTDIR y audit ELF antes de publicacion.
- Rust 1.98.1 release locked/offline por separado.
- CUSTOM V2: conservar Essential/delta y hashes DEB, salvo evidencia de cambio real de material.
- Boss Registry: actualizar solo despues de certificar binarios y publicar commit exacto de Connect y Construction.
- Aceptacion en Ryzen via Boss: escalado VNC, mouse X/Y, seleccion de monitor, logo, Quick/My, credenciales preservadas, y recuperacion tras cierre.

La validacion estructural C63 de 14/14 pruebas es anterior al ajuste de metadatos de 1.0.7; repetir los gates sobre el arbol final.
