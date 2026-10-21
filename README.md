# N.E.E.B.L.E.S. Connect 1.0.1

Módulo de acceso remoto SSH/VNC integrado (Qt 6.8.2, Rust 1.98.1). Instalación y ejecución gobernadas por Boss mediante CUSTOM V2 y RuntimeLease.

## Modelo 1.0.1

- **Quick Connect:** conecta mediante host, usuario, clave y puerto manuales, sin guardar credenciales; no depende de desbloquear Secrets.
- **My Connect:** guarda y recupera equipos únicamente por la API cifrada de **NEEBLES Secrets 1.0.2**, en `vault-connect.kdbx`. Alta, consulta, actualización y eliminación de registros. Cada registro identifica host, nombre, usuario, contraseña y puertos SSH/VNC.
- **Bloqueo de Secrets:** deshabilita My Connect, cierra solo las pestañas y conexiones abiertas desde My Connect, elimina metadatos cargados y vuelve al Dashboard si corresponde. Las conexiones Quick permanecen.
- **Arranque:** puede abrirse con Secrets bloqueado. My Connect recupera registros después de desbloquear, sin reconectar automáticamente.
- **Instalación:** `lifecycle.json` solicita a Secrets la bóveda del módulo mediante `construction.step/install-vault`. Los datos KDBX y la Master Key están fuera del repositorio y deben persistir durante Update.

No se usa `profiles.dev.json`, SQLite, credenciales en Settings ni viewers externos. La finalidad de `vault-general.kdbx` aún no está definida: Connect **no** debe usarla.

## Archivos productivos

- `runtime/src/main.rs` → IPC con Boss y supervisor de UI.
- `ui/desktop/` → Dashboard, Quick Connect, My Connect, SSH/libssh2, VNC/libvncclient y terminal libvterm.
- `manifest.json`, `lifecycle.json`, `contracts/`, `languages/`, `settings/` → contratos gobernados.
- `tooling/esbirro/build_connect_101.py` → única receta de Qt 6.8.2 controlado, respaldada por SHA256 de material CUSTOM V2; genera DESTDIR en caché, nunca altera `/opt`.
- `tests/test_connect_101_contracts.py` → controles estructurales actuales, no equivalen a aceptación real.

## Compilación y certificación

Rust 1.0.1 ya compiló en el Ryzen (`cargo test --locked` y `cargo build --locked --release` GREEN). Corresponde compilar **la UI Qt modificada con Esbirro**, certificar ELF y después incorporar los binarios Release al repositorio. **No se recompila Boss, Secrets ni toda la distribución**.

```bash
python3 tooling/esbirro/build_connect_101.py --preview
python3 tooling/esbirro/build_connect_101.py --build
python3 -m unittest discover -s tests -p 'test_connect_101_contracts.py' -v
```

Al publicar: commit Connect 1.0.1 → pin SHA en CUSTOM V2 `fetch/checkout` → actualización de Registry Boss → instalación/actualización oficial por Boss → pruebas reales de Quick/My, lock/unlock y persistencia KDBX. Nunca modificar `/opt/neebles` a mano.

## Riesgos a validar en instalación real

- Autorización IPC de Secrets para el ejecutable Connect realmente instalado bajo RuntimeLease.
- VNC y SSH simultáneos desde My; bloqueo remoto cierra My sin interrumpir Quick.
- Persistencia, operaciones KDBX y no exposición de secretos por logs o archivos secundarios.
- Rendimiento de VNC: procesamiento gráfico en el hilo Qt sigue como limitación conocida.
