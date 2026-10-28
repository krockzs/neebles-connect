# NEEBLES Connect — modelo KDBX preparado (Q02)

Estado: fuente en desarrollo, sin migración ni escritura sobre la bóveda instalada.

## Cuatro protocolos, NO cinco

La interfaz de My Connect tiene cuatro familias: SSH (0..1), VNC (0..1),
VS Code (0..N) y Sunshine/Moonlight (0..1). Los datos generales del equipo
no son un protocolo ni obligan a crear una conexión ficticia.

El motor `structure.*` de Secrets no admite objetos anidados ni reglas
condicionales entre campos. El modelo físico usa **dos esquemas**, no cinco:

- `myconnect`: 0..N registros de equipos, con `name` e `ip` obligatorios,
  `user` general opcional y tres grupos independientes **opcionales**
  `ssh-*`, `vnc-*`, `sunshine-*`. Un equipo recién creado omite todos esos
  grupos. No existe un `node` ni una entrada de SSH vacía obligatoria.
- `vscode`: 0..N registros por equipo; `equipment` referencia un ID existente
  de `myconnect`, con `name`, `port` (1..65535), `pass` opcional.
  Unicidad real de Secrets por (`equipment`, `port`).

Los grupos SSH/VNC/Sunshine son unitarios por construcción: cada equipo
es un único registro `myconnect` y no pueden duplicarse sin duplicar el equipo.
Cada credencial tiene su propio campo secreto.

Secrets valida campos, tipos, referencias y unicidad del puerto VS Code;
Connect valida integridad de los grupos opcionales (por ejemplo, si existe
`ssh-name` deben existir también `ssh-user` y `ssh-port`) porque el contrato
actual de Secrets no admite `required_if`. Los rangos de puerto 1..65535
se validan antes de `structure.put` porque el campo `integer` de Secrets
no tiene restricciones de mínimo/máximo.

Moonlight no tiene una instancia ni un esquema propio. El emparejamiento
Sunshine/Moonlight se completará cuando se implemente el motor.

## Gobierno de los datos

- Propietario: `neebles-secrets` mediante broker privado autenticado.
- Bóveda: la bóveda del módulo Connect, garantizada por Secrets, no por Qt.
- Crear esquema: `structure.schema_define` solo si no existe, idempotente
  si es idéntico; diferencias exigen migración explícita.
- CRUD: `structure.put/get/list/delete` con revisiones, sin JSON secundarios.
- Los borrados de equipos con referencias VS Code son rechazados por Secrets.
- Quick Connect no se persiste ni consume Secrets.
- Esta etapa NO modifica `lifecycle.json`, no borra KDBX, no cambia `/opt` y
  no habilita todavía estos métodos desde la interfaz 1.0.1 heredada.
  La integración UI/CRUD reemplazará el legado en el siguiente incremento.

La retirada excepcional de la antigua bóveda de Connect es externa a las
operaciones Install/Update/Reinstall y necesita ejecución explícita durante
la futura actualización instalada. Nunca será parte del updater.
