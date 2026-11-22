# Esbirro: construcción permanente de NEEBLES Connect

**Entrada única para todas las releases nuevas:** `tooling/esbirro/build_connect.py`.

## Separación de responsabilidades

- **Versión de Connect:** `manifest.json` y `runtime/Cargo.toml` deben coincidir. El constructor detecta cualquier versión válida sin cambiar Python.
- **Fuentes:** inventario automático desde los archivos rastreados por Git HEAD, verificados contra los blobs exactos; se rechazan cambios locales y archivos no rastreados. Acepta `main`, ramas de preparación y checkout `--detach`, siempre que Git esté limpio.
- **Materiales de Esbirro:** `tooling/esbirro/connect-build-profile.json`, versionado con Git, contiene Qt, GCC, sysroot, paquetes CUSTOM V2 con SHA-256, parche Q06 y excepción ELF de `libproxy`. **No contiene la versión de Connect.**
- **Ejecución:** prepara únicamente candidatos bajo `~/Downloads/NEEBLES_CONNECT_<versión sin puntos>_CANDIDATOS/`; ningún comando instala ni publica el módulo ni cambia Boss/Secrets.
- **Certificaciones independientes:** `--build` verifica CMake, DESTDIR y dependencias ELF directas. El cierre recursivo de ELF y la aceptación RuntimeLease de Boss se ejecutan y documentan por separado. No llamarlos PASS antes de ejecutarlos.

## Comandos

Desde Ryzen en terminal NEEBLES Connect:

```bash
python3 -B ~/NEEBLES/neebles-connect/tooling/esbirro/build_connect.py --preview \
  --connect ~/NEEBLES/neebles-connect \
  --custom ~/NEEBLES/neebles-custom
```

Después de la validación, únicamente para crear un candidato aislado:

```bash
python3 -B ~/NEEBLES/neebles-connect/tooling/esbirro/build_connect.py --build \
  --connect ~/NEEBLES/neebles-connect \
  --custom ~/NEEBLES/neebles-custom
```

Pruebas de regresión:

```bash
cd ~/NEEBLES/neebles-connect
python3 -B -m unittest discover -s tests -p 'test_*.py' -v
```

## Cuándo hay que modificar algo

| Cambio | Modificación necesaria |
|---|---|
| Connect 1.0.8, 1.0.9, 2.0.0, etc. | Actualizar los manifiestos de versión y las fuentes de Connect. **No editar el constructor ni el perfil si sigue vigente el mismo material.** |
| Nuevo compilador, Qt o sysroot de Esbirro | Actualizar el perfil de materiales, comprobar compatibilidad de CMake y recertificar. |
| Nuevo DEB, parche Q06, ABI o `RUNPATH` | Actualizar SHA/versiones autorizadas en el perfil, realizar auditoría física y recertificar. No agregar una excepción universal. |
| Cambio sustancial en el procedimiento de construcción | Modificar `build_connect.py` y sus pruebas, solo por razones técnicas reales. |

Las recetas `build_connect_101.py`, `build_connect_103.py`, `build_connect_104.py` y `build_connect_107.py` son **historiales**, no la entrada de mantenimiento futuro. Se mantienen de momento como evidencia de los laboratorios previos.

## Seguridad

El perfil no puede apuntar a materiales fuera de las rutas controladas; paquetes, manifiestos CUSTOM, encabezados, bibliotecas y RUNPATH se verifican físicamente. El `RUNPATH` autenticado de `libproxy` se proyecta al sysroot de Esbirro; nunca se interpreta como permiso para cargar bibliotecas del anfitrión.

Un PASS del constructor o un cierre ELF completo **no implica** que RuntimeLease, Secrets, SSH, VNC, múltiples monitores o actualización desde Boss hayan sido aprobados. El procedimiento de publicación conserva esas puertas de aceptación.
