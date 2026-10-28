# Connect Q08I — trazabilidad de construcción controlada

Autoridades: `README_CUSTOM.md`, `README_CUSTOM_V2.md`, `portable/esbirro/README.md` de `neebles-custom`. La autoridad de material y certificación pertenece a Esbirro/CUSTOM V2; Boss controla la ejecución y RuntimeLease. No confundir un build exitoso con certificación de runtime.

## Fuente declarada del DEB Q08D

- Debian source: `libvncserver 0.9.15+dfsg-1+deb13u2` (`3.0 quilt`).
- Conserva la serie `0001_CVE-2026-32853.patch`, `0002_CVE-2026-32854.patch`, `0003_CVE-2026-44988.patch`, `0004_CVE-2026-50538.patch`.
- Adaptador versionado: `tooling/esbirro/aplicar_fuentes_libvncclient_q06.py`.
- Patch quilt versionado: `tooling/esbirro/patches/neebles-q06-extendeddesktopsize.patch`, SHA-256 `d6785f5623d123cdbd88d8115fa3cab9ba9084a9fe6dcf62fb369891b7c34c50`.
- Header original `include/rfb/rfbclient.h`: `422e9afa2aff6a38beb4667ea9e1ae9be4e3c421af9ab163d70b5236302a04cd`.
- Header Q06: `aaa8c2cf639ad8bd8a61be1f7a109050a99b0a9546b6f5f093528d921b6d604c`.
- Fuente `src/libvncclient/rfbclient.c` original: `7010aba9a90ab9eebf3cffa54125c28b152099cdf2bbf6a59a89bf3271377a42`.
- Fuente Q06: `eb008639b90a79e63d9e5b805ac4a30951ab49c0a9671eac41cb24946acfddc6`.
- DEB Q08D: `libvncclient1_0.9.15+dfsg-1+deb13u2+neebles1_amd64.deb`, SHA-256 `57572d40da915b9e648c33f8c386f647176a146a06b633ba7a2893ce6306c9da`.
- ELF dentro del DEB Q08D: `dfd203cc5b291c5b61d34972102b5732a7d75108a754ae7f321c221034dbe925`.

## Reproducir

1. Obtener y validar la fuente Debian exacta (`.dsc`, `.orig.tar.xz`, `.debian.tar.xz`), comprobando SHA-256 a través del descriptor `.dsc`. La preparación previa Q08B ya lo hizo en `~/Downloads/NEEBLES_CONNECT_Q08B_FUENTE_DEBIAN/`.
2. Aplicar el patch versionado a una **copia** de la fuente Debian; comprobar el SHA-256 de ambos archivos transformados y de los cuatro parches CVE.
3. Construir LibVNCClient con GCC14 y dependencias del mundo controlado Esbirro; comparar ABI/SONAME/`DT_NEEDED` con el DEB original. El candidato Q08D ya superó estos gates físicos.
4. `build_connect_101.py` versionado toma el DEB Q08D autenticado por CUSTOM V2 y la cabecera Q06 correspondiente. Requiere la fuente Debian validada en el directorio Q08B, que es un **insumo verificado**, no una fuente de autoridad por sí misma.
5. El build de Qt solo produce un nuevo candidato en `~/Downloads`; **no repite automáticamente el gate ELF físico completo**. Debe realizarse la auditoría Q08E-R4R2 o su sucesora sobre ese candidato y, posteriormente, aceptación real bajo RuntimeLease.
6. Cualquier cambio físico posterior en el laboratorio Esbirro obliga a actualizar sus snapshots y `portable/esbirro/manifest.json`. Q08I **no** cambia el laboratorio y no requiere resellar snapshots.

## Referencias binarias Q08G

- Qt: `416ecbce17ecb2bf7db438c93912b4cbe6be950d3a850cbc22323369fdb3ac42`.
- Rust: `b9154c0690bd581917a0457a671aafc43a3561c173d906b9f4e10c6e4eb24405`.

**Certificación de RuntimeLease: pendiente. Publicación: pendiente.**
