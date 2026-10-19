# Integración CUSTOM V2 de NEEBLES Connect

Esta carpeta **pertenece al repositorio de trabajo neebles-connect**.
`candidates/` contiene material de integración en desarrollo, NO material
certificado ni aprobado para publicar en CUSTOM V2.

La autoridad de membresía, integridad, RootFS, mundos y Construction productivos
continúa siendo `neebles-custom/runtime/`.

Los candidatos se generaron con un script que reprodujo el RootFS del Test
Module y comparó las entradas físicas, pero aún deben superarse los gates
de Esbirro, materialización Boss, ELF/ABI, permisos y runtime real.
Ningún fichero aquí autoriza por sí solo una instalación.

Los scripts ejecutables y los informes TXT de trabajo se guardan en Downloads.
Antes de integrar un candidato en CUSTOM V2, revisar el mundo SSH/VNC,
los contratos efectivos y los grants explícitos.
