# NEEBLES Connect — Q06 RFB multimonitor / fuentes

## Evidencia real (Ryzen, 27-oct-2026)

- `X0tigervnc`, TigerVNC Scraping 1.15.0, puerto 5900, Tailscale `100.124.177.12` en la sesión de diagnóstico.
- Protocolo `RFB 003.008`, `VncAuth` OK.
- Respuesta verificada: `ExtendedDesktopSize framebuffer=3280x1080, screens=2, reason=0, status=0`.
- Pantalla #1 `id=1804289383`, `x=0`, `y=0`, `1360x768`.
- Pantalla #2 `id=846930886`, `x=1360`, `y=0`, `1920x1080`.
- **Es información recibida por RFB**, no inferida del tamaño del framebuffer.

## Fuentes Q06

1. La fuente de LibVNCClient `0.9.15` recibe todos los descriptores RFB pero expone solo `client->screen`. El adaptador adjunto incorpora `GotNeeblesScreenLayout` para entregar todos los rectángulos en orden de bytes nativo.
2. `VncPane` copia los monitores del callback antes de retornar; ningún puntero del buffer RFB queda persistido.
3. Cada VNC tiene `monitors_`, `selectedMonitor_` y `selectedMonitorId_` propios. En un cambio de topología, se conserva la selección por ID si sobrevive; si desaparece, se vuelve al escritorio completo.
4. `VncScreen` ajusta imagen/ratón/rueda a la región original; el Copy usa `QImage::copy` para llevar al portapapeles solo esa región. `-1` significa framebuffer completo.
5. Barra contextual única: icono de escritorio completo, luego uno por cada descriptor RFB válido; el tooltip indica número, resolución e ID. No se crean iconos para pantallas inferidas.
6. Sin llamadas a `SetDesktopSize`, sin XRandR remoto por SSH y sin depender de systemd para identificar monitores.

## Material controlado requerido (NO materializado ni publicado por este parche)

- Aplicar `aplicar_fuentes_libvncclient.py` **únicamente** sobre fuentes verificadas `libvncserver/libvncclient` de la misma procedencia/versión Debian `0.9.15+dfsg-1+deb13u2` y con todos sus parches de seguridad de Debian conservados.
- Reproducir compilación/empaquetado en Esbirro y producir DEB controlados para biblioteca runtime **y desarrollo/header**. Nunca sustituir `/usr/lib`, `/opt`, RootFS o pools certificados a mano.
- En `neebles-custom`, actualizar solo el delta correspondiente de Connect (membresía, hashes, manifiestos de integridad y mundo/Construction si fuese necesario), preservando el material Essential y de otros módulos.
- El CMake de Connect tiene gate fatal: `NEEBLES_RFB_TOPOLOGY_V1` en header y símbolo exportado `rfbNeeblesTopologyApiVersion() == 1`. No recompilar Connect contra la biblioteca stock con el header extendido.
- Ejecutar QA de compilación/funcional y certificar conjuntamente con los demás cambios **al final**, conforme a la decisión vigente. El aplicador Q06 modifica solamente fuentes Connect.

## Restricciones de alcance

- La llamada LibVNCClient `rfbInitClient`, `HandleRFBServerMessage` y el IPC síncrono Secrets siguen en el hilo GUI en este hito. Por tanto Q06 no constituye la corrección de congelamientos.
- La fase obligatoria **Q07** resolverá ese bloqueo antes de cerrar y publicar la release. Véase `docs/CONNECT_Q07_RESILIENCIA_PENDIENTE.md`.
- Q06 aquí significa **fuentes integradas, no build ni aceptación live**. Hay un requisito de certificación CUSTOM V2 todavía abierto.
