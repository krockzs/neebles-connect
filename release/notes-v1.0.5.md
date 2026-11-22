# NEEBLES Connect 1.0.5

Corrección del Lifecycle update: se restaura el contrato de actualización de Connect 1.0.1.

La instalación inicial sigue ejecutando ensure-secrets-vault. Una actualización no vuelve a ejecutar install-vault.

La bóveda de Secrets y sus conexiones guardadas permanecen fuera del repositorio y no se modifican.

Rust se recompila para 1.0.5; Qt mantiene exactamente el ejecutable certificado de 1.0.4, sin cambios gráficos.

La aceptación final requiere una actualización real mediante Boss.
