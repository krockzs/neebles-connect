# N.E.E.B.L.E.S. Connect

**NEEBLES Connect** es el módulo de NEEBLES OS para administrar conexiones remotas desde una única aplicación de escritorio. Integra terminales SSH, escritorios VNC, conexiones temporales y equipos guardados, con organización visual personalizada y protección de los datos mediante NEEBLES Secrets.

**Estado actual:** operativo e integrado con NEEBLES Boss. Las conexiones **SSH y VNC** están implementadas y han sido utilizadas satisfactoriamente en el Ryzen, incluida la actualización del módulo desde Boss.

**Integraciones pendientes:** VS Code y Sunshine/Moonlight.

## Funcionalidades

| Área | Funcionalidad |
| --- | --- |
| **Dashboard** | Vista de conexiones guardadas, grupos, separadores, orden y estados de las sesiones. |
| **Quick Connect** | Conexiones temporales sin guardar credenciales ni equipos en la bóveda. |
| **My Connect** | Administración de equipos, configuraciones de conexión, grupos y colores personalizados. |
| **SSH** | Terminal interactiva con historial, representación ANSI/Unicode, selección, copia y pegado. |
| **VNC** | Escritorio remoto, multimonitor real, selección de pantalla, copia de imagen y escalado al panel. |
| **Pestañas** | Navegación por equipos y protocolos, selección de sesiones y orden persistente. |
| **Secrets** | Custodia de credenciales, datos de equipos y organización cifrada. |
| **Boss** | Instalación, actualización, apertura y cierre gobernados del módulo. |

## Interfaz y experiencia de uso

La aplicación utiliza un tema oscuro con fondo negro, acentos violetas y cian, controles de contraste y avisos breves. El diseño está organizado en una barra lateral y un área de trabajo con pestañas.

### Navegación y pestañas

- La barra lateral contiene **Dashboard**, **Quick Connect** y **My Connect**.
- La pestaña administrativa permanece fija en la primera posición; las pestañas de equipos se pueden reorganizar.
- El orden de las pestañas de equipos se conserva mediante el documento cifrado de organización de Secrets.
- Al seleccionar una conexión SSH o VNC activa, la barra lateral pasa a **modo compacto** para dejar más espacio al contenido. Al volver a una vista administrativa se expande.
- La barra lateral expandida muestra el logo oficial; la compacta presenta los accesos e instrumentos contextuales.
- Las acciones **Conectar** y **Desconectar** corresponden siempre al protocolo seleccionado, sin afectar otras sesiones abiertas.
- Los avisos breves informan sobre acciones de la interfaz sin reemplazar el seguimiento de las operaciones.

### Identidad visual

| Elemento | Color |
| --- | --- |
| Fondo principal | `#09090B` |
| Violeta de controles | `#7C3AED` |
| Violeta de énfasis | `#A855F7` |
| Cian de controles activos | `#22D3EE` |
| Texto cian de énfasis | `#67E8F9` |
| Texto principal | `#F5F5F5` |

Los colores de los equipos son **personalizables** y no están limitados a esta paleta. Cada equipo puede conservar el aspecto predeterminado, sin color asignado, o utilizar un color elegido por el usuario.

Cuando un equipo tiene color personalizado, este se refleja en su presentación y **rellena toda su pestaña superior**. El texto se adapta automáticamente a la luminosidad del fondo para conservar el contraste. El aspecto predeterminado no se sustituye por un color obligatorio.

## Dashboard

Dashboard reúne los equipos registrados y ofrece acceso a sus conexiones configuradas.

- **«Sin equipo» aparece siempre primero**, seguido de los grupos personalizados.
- Los equipos nuevos sin grupo se incorporan al comienzo de «Sin equipo».
- Se pueden crear, renombrar, colorear y reordenar separadores o grupos personalizados.
- Los encabezados de grupo tienen **fondo completo del color elegido**, contraste automático y separadores visuales.
- Los equipos muestran su color personalizado y los accesos a los protocolos configurados.
- Los indicadores de conexión SSH y VNC representan el estado de las sesiones, no una suposición sobre la disponibilidad del equipo remoto.
- Seleccionar un acceso del Dashboard lleva a su pestaña; no establece automáticamente una nueva conexión.
- La organización y el orden se conservan en Secrets.

## Quick Connect

Quick Connect permite trabajar con un equipo remoto **sin incorporarlo a My Connect**.

- Solicita los datos de conexión necesarios para la sesión temporal.
- Las credenciales temporales **no se guardan** como perfiles persistentes.
- SSH y VNC pueden iniciarse directamente desde la acción de conexión.
- Funciona de forma independiente del desbloqueo de My Connect.
- Las conexiones temporales no se confunden con los equipos guardados.

## My Connect

My Connect administra la información persistente de los equipos. Cada equipo dispone de un identificador estable, nombre y dirección; las configuraciones de protocolo se añaden solamente cuando se necesitan.

- Crear, editar y eliminar equipos desde una vista de tarjetas.
- Consultar y administrar las configuraciones de conexión de forma independiente.
- Asignar equipos a **«Sin equipo»** o a grupos personalizados.
- Conservar el orden de presentación al editar información que no cambia la agrupación.
- Seleccionar un color personalizado o volver al aspecto predeterminado.
- Abrir conexiones SSH y VNC desde las tarjetas correspondientes.
- Mantener diferenciados los datos de cada equipo y sus credenciales.

Los equipos y la organización pertenecen a la bóveda de Connect administrada por NEEBLES Secrets. No se usan archivos locales en texto plano para almacenar las credenciales de My Connect.

## Conexiones SSH

SSH utiliza **libssh2** para el transporte y **libvterm** para la terminal integrada en Qt.

- Terminal interactiva con entrada de teclado, redimensionamiento y desplazamiento por el historial disponible.
- Representación de colores ANSI, Unicode, caracteres de ancho doble y estilos de texto.
- Selección de texto con el ratón y copia de la selección mediante **Ctrl+Shift+C**.
- Pegado desde el portapapeles mediante **Ctrl+Shift+V** o clic derecho.
- El botón contextual **Copiar** envía al portapapeles la salida disponible de la sesión, incluido el historial conservado en memoria.
- Comprobación de identidad de la clave del servidor SSH antes de autorizar una conexión.
- Trabajo de red separado del hilo gráfico para evitar bloquear la interfaz ante conexiones o cierres lentos.

## Conexiones VNC

VNC utiliza **LibVNCClient** con soporte de topología remota de monitores obtenida del protocolo RFB.

- Visualización del escritorio completo o de un monitor específico.
- Identificación de monitores mediante los descriptores reales recibidos del servidor, sin inferirlos de la resolución total.
- Selección de monitor y conservación de su identidad mientras permanezca disponible.
- El contenido remoto **ocupa toda la superficie del panel**; la transformación del ratón utiliza escalas independientes en X e Y y respeta el origen del monitor seleccionado.
- El botón contextual **Copiar** envía al portapapeles una imagen del monitor seleccionado, o del escritorio completo cuando no existe una selección individual.
- Procesamiento de red y fotogramas separado de la interfaz Qt, con estados de conexión y desconexión controlados.

## Datos, seguridad y ciclo de vida

Connect trabaja con **NEEBLES Secrets** como autoridad sobre la bóveda y los registros persistentes. La aplicación consume las operaciones de estructuras del broker; no manipula directamente archivos KDBX.

- `myconnect`: datos generales del equipo y configuraciones opcionales.
- `dashboard`: documento cifrado con grupos, colores, orden y distribución.
- La información de las conexiones se valida antes de persistirse y utiliza revisiones para detectar conflictos de escritura.
- El bloqueo de Secrets restringe las operaciones de My Connect que dependen de la bóveda. Quick Connect mantiene su independencia.
- **Instalación inicial:** Boss solicita la preparación de la bóveda de Connect mediante Secrets.
- **Actualización:** no se vuelve a inicializar ni se elimina la bóveda; se preservan los equipos y credenciales guardados.
- **Apertura y cierre:** Boss gobierna la sesión del módulo mediante su runtime y los contratos de IPC.

No se debe manipular manualmente el directorio instalado, los archivos KDBX, los paquetes ni los materiales de ejecución para actualizar Connect.

## Arquitectura

```
NEEBLES Boss
  └── Runtime y gobierno del módulo
        └── Connect · supervisor Rust
              └── Aplicación Qt
                    ├── Dashboard / Quick Connect / My Connect
                    ├── Terminal SSH · libssh2 + libvterm
                    ├── Escritorio VNC · LibVNCClient / RFB
                    └── Broker NEEBLES Secrets
                          └── Bóveda y organización cifrada
```

| Capa | Responsabilidad |
| --- | --- |
| **Qt / C++** | Interfaz, navegación, terminal, presentación remota y acciones de usuario. |
| **Rust** | Supervisor del módulo, sesión IPC y ciclo de vida del proceso gráfico. |
| **Secrets** | Custodia de credenciales, registros estructurados y organización persistente. |
| **Boss** | Autoridad de instalación, actualización, ejecución y cierre. |
| **CUSTOM** | Material de construcción, RootFS `rust-qt` y dependencias controladas. |
| **Esbirro** | Construcción reproducible de la aplicación con materiales autenticados. |

## Construcción y mantenimiento

La entrada permanente de compilación Qt es **`tooling/esbirro/build_connect.py`**. El perfil **`tooling/esbirro/connect-build-profile.json`** define los materiales autorizados: compilador, Qt, sysroot, paquetes y comprobaciones de dependencias.

El constructor obtiene la identidad de publicación desde los manifiestos del proyecto, verifica las fuentes Git y prepara **candidatos aislados**. No instala Connect ni altera Boss o Secrets. Las recetas antiguas se conservan únicamente como evidencia histórica, no como procedimientos de mantenimiento.

Desde la raíz del repositorio:

```bash
python3 -B -m unittest discover -s tests -p 'test_*.py' -v
```

Para revisar los materiales antes de construir:

```bash
python3 -B tooling/esbirro/build_connect.py --preview \
  --connect . \
  --custom ../neebles-custom
```

La compilación, la auditoría de dependencias ELF y la aceptación bajo Boss son verificaciones distintas. Una compilación satisfactoria no sustituye las pruebas de funcionamiento real.

La instalación y la actualización del módulo se efectúan **desde NEEBLES Boss**, nunca ejecutando directamente los candidatos de compilación como sustituto del proceso gobernado.

## Referencias del repositorio

- [Guía de construcción con Esbirro](tooling/esbirro/README_BUILD_CONNECT.md)
- [Manifiesto del módulo](manifest.json)
- [Contrato de ciclo de vida](lifecycle.json)
- [Estructuras y persistencia](ui/desktop/ConnectStructures.cpp)
- [Interfaz principal](ui/desktop/MainWindow.cpp)
- [Terminal SSH](ui/desktop/SshTerminal.cpp)
- [Sesión VNC](ui/desktop/VncPane.cpp)
- [Pruebas automatizadas](tests/)

Los archivos de `docs/` y `release/` conservan antecedentes técnicos de desarrollo. Para conocer las capacidades vigentes, este README y el código actual son la referencia principal.
