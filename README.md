# N.E.E.B.L.E.S. Connect 1.0.2

**Estado documental: 28-10-2026.** Connect **1.0.2** está publicado en Git `main`, seleccionado por CUSTOM V2 Construction y por el Registry de Boss. El operador informó **aceptación funcional en el Ryzen** el 28-10-2026: VNC con dos monitores, guardado de equipos y credenciales/usuarios separados, y funcionamiento general de las superficies probadas. El alcance de esa aceptación es el laboratorio informado; no implica certificación universal de cualquier hardware o protocolo futuro.

## Funciones y modelo de datos

- **Quick Connect:** conexión manual, sin guardar secretos en disco; sus sesiones no dependen de que My Connect esté desbloqueado.
- **My Connect:** equipos persistidos mediante `structure.*` de **NEEBLES Secrets 1.0.7** en la bóveda exclusiva de Connect. Cada equipo contiene datos generales y grupos opcionales de SSH, VNC y Sunshine; no se exige crear conexiones vacías.
- **VS Code:** modelo de credenciales con cero o más instancias por equipo y unicidad por equipo/puerto. La existencia del modelo no significa aceptación del motor remoto de VS Code en todos los ambientes.
- **Dashboard:** disposición persistida mediante estructura cifrada en Secrets, no en `profiles.dev.json` ni en archivos JSON de credenciales.
- **SSH:** libssh2 + terminal libvterm, con worker y cancelación de conexión/cierre acotado.
- **VNC:** LibVNCClient 0.9.15 adaptada a `ExtendedDesktopSize`, selección explícita de monitores RFB por ID, escritorio completo y proyección de coordenadas. Prueba real comunicada de **dos monitores**.
- **Sunshine/Moonlight:** campos y superficies contemplados en el diseño. El emparejamiento/motor completo requiere una aceptación específica; no declararlo terminado únicamente por el éxito VNC/SSH.
- **Supervisor:** runtime Rust y ventana Qt, registro IPC, cierre gobernado por Boss y tratamiento de errores/cortes sin bloqueo indefinido de UI según Q07.

Los esquemas son `myconnect`, `vscode` y `dashboard` (`ui/desktop/ConnectStructures.cpp`); Secrets gestiona la validación, el KDBX y las claves. La política del Registry exige que Secrets esté instalado con `master_key="true"` y `unblock="true"` **para instalar** Connect. El bloqueo posterior deshabilita las operaciones que requieren bóveda, sin convertir Quick en consumidor de Secrets.

## Construcción y materiales

| Capa | Autoridad y material |
|---|---|
| C++ / UI | Qt **6.8.2**, GCC **14**, Esbirro controlado, `bin/neebles-connect-desktop` |
| Rust | **1.98.1**, `runtime/neebles-connect` 1.0.2 y Cargo locked/offline |
| VNC Q06 | `libvncclient1_0.9.15+dfsg-1+deb13u2+neebles1_amd64.deb`, SHA-256 `57572d40da915b9e648c33f8c386f647176a146a06b633ba7a2893ce6306c9da` |
| RootFS | CUSTOM V2 `rust-qt`, world `modules.rust-qt`, Essential + delta propio |
| Ejecución | Boss MaterialBinding, persistent RootFS y RuntimeLease; ningún fallback al host |

El material Q06 se deriva de la fuente Debian y conserva los parches de seguridad. El parche reproducible y el adaptador están versionados en `tooling/esbirro/patches/` y en el directorio `tooling/esbirro/`. `build_connect_101.py` y `connect-q08g-release-candidate.json` mantienen trazabilidad **histórica** del candidato Qt que fue reutilizado sin recompilar al pasar a 1.0.2. No deben interpretarse como identidad vigente del runtime Rust.

## Historial de cierre Q08–Q09

1. **Q08D/Q08E:** nueva biblioteca RFB construida con Esbirro, Qt enlazado y compilado al 100 % en DESTDIR temporal.
2. **Q08E-R4R2:** auditoría ELF recursiva de **89 objetos**, **cero** dependencias `DT_NEEDED` irresueltas. Los RUNPATH de `libproxy` y `libpxbackend` se verificaron físicamente contra DEB autenticado; su confinamiento efectivo sigue siendo cuestión de la frontera RuntimeLease real.
3. **Q08F/Q08G:** pruebas C++/Rust y staging de los ejecutables candidatos.
4. **Q08I–Q08L:** fuentes reproducibles versionadas, Connect y material CUSTOM V2 publicados; Registry Boss actualizado.
5. **Q09A:** versión 1.0.2 en manifiesto, Cargo/lock, validación Rust y Lifecycle; pruebas estructurales **13/13**, Rust **5/5**, rustfmt y Release aprobados.
6. **Q09B/Q09C:** ELF Rust 1.0.2 autenticado con SHA-256 `0fd8223f930e3cb8ad12d26cbb2c33e38488edc383d98384d5618de9dda14b4b`; commit Connect **`2e6390268ea85935d7118b7276adb092c0328f0d`** publicado.
7. **28-10-2026:** el operador reporta pruebas funcionales satisfactorias con dos monitores, equipos y credenciales independientes. Conservar ese resultado como aceptación **informada**; para una auditoría formal adicional adjuntar logs de RuntimeLease y matriz de casos.

Un commit documental posterior al commit Q09C **no cambia los bytes de la release ni autoriza repinear automáticamente Construction/Registry**. El pin actualmente seleccionado continúa identificando el código y ejecutables de 1.0.2.

## Lifecycle e integración de Secrets

`lifecycle.json` utiliza `construction.step` sobre Boss: `install` y `update` solicitan `install-vault` (`vault.ensure`, idempotente); `open` ejecuta `open-runtime` en sesión persistente. La bóveda `vault-neebles-connect.kdbx` es propiedad exclusiva de Secrets; no se accede a ella mediante KDBX directo ni se borra en Update.

La transición 1.0.1 → 1.0.2 acordó una renovación **única y explícita** de la antigua bóveda de Connect para adoptar los nuevos esquemas. La operación no forma parte del instalador: únicamente puede autorizarse tras verificar la ruta real, las sesiones y el estado del servicio. `vault-general.kdbx`, la Master Key y las bóvedas de otros módulos se preservan. No se habilita como política general para futuras releases.

## Pruebas y operación

La receta de UI histórica está en `tooling/esbirro/build_connect_101.py`; el contrato de fuentes Q06 en `tooling/esbirro/README_Q08I_REPRODUCIBILIDAD.md`. Los contratos estructurales pueden verificarse con:

```bash
python3 -B -m unittest discover -s tests -p 'test_*.py' -v
cd runtime && cargo fmt --all -- --check && cargo test --locked --offline
```

No usar `ldd` del host como sustituto de cierre ELF de Esbirro. Instalación y actualización se realizan **exclusivamente desde Boss**, con el RootFS y las autorizaciones declaradas en CUSTOM V2. Para continuidad consultar `release/notes-v1.0.2.md`, `docs/CONNECT_MYCONNECT_STRUCTURE_V1.md`, `docs/CONNECT_Q06_RFB_TOPOLOGIA.md` y el historial de Q07.

## Estado de publicación seleccionado

| Repositorio | Selección de código/material (no commit documental) |
|---|---|
| Connect 1.0.2 | `2e6390268ea85935d7118b7276adb092c0328f0d` |
| CUSTOM V2 material/Construction | `0bb4478d9feae938714c9ca678dcd26dc0a04f86` |
| Boss Registry | `b559588312817dcc9166a6e006d1a49d5019e250` |
| Secrets 1.0.7 | `d61e3aa916d7c5863ae7dc9bb29661b711753a30` |

La prueba funcional informada no sustituye el cierre documentado de otros protocolos, reinicios adversariales, rollback, recuperación y portabilidad. No modificar manualmente `/opt/neebles`, KDBX, paquetes ni autoridades de Esbirro para resolver errores de un consumidor.
