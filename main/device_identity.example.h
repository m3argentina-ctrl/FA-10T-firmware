#pragma once
//
// PLANTILLA de identidad de equipo. NO se usa tal cual.
//
// Estos valores son sólo el FALLBACK de compilación para banco/desarrollo,
// cuando el equipo todavía no tiene la partición NVS "factory" provista.
// En producción la identidad real se pre-carga de fábrica en esa partición
// (ver tools/provision/) y PISA estos defaults.
//
// Uso en desarrollo:
//   1. Copiar este archivo a  main/device_identity.h   (está gitignored).
//   2. Completar dev_id / token / URL del equipo de prueba.
//   3. Compilar y flashear. cloud_telemetry.c lo incluye con __has_include().
//
// NUNCA commitear device_identity.h con un token real.

#define DEVICE_ID_DEFAULT       "FA10T-DEV-0000"
#define DEVICE_TOKEN_DEFAULT    "dev-token-cambiar"
#define CLOUD_BASE_URL_DEFAULT  "https://bioorigen-web.vercel.app"
