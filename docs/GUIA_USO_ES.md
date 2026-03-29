# Guía paso a paso (ES): uso completo de `libcpujitter` + `cpujitter-qualifier`

Esta guía explica cómo:
1. preparar dependencias,
2. compilar y probar,
3. ejecutar el runtime,
4. generar/curar perfiles con el qualifier,
5. integrar `libcpujitter` en otra aplicación.

---

## 1) Qué incluye este repositorio

- **`libcpujitter` (runtime)**: librería C para obtener bytes aleatorios y utilidades (por ejemplo, d6 justo), con perfiles JSON, cache local validada, smoke test y recalibración ligera.
- **`cpujitter-qualifier` (tooling interno)**: herramienta para explorar parámetros y exportar perfiles (no es dependencia de runtime para app final).

---

## 2) Preparar dependencias externas

Desde la raíz del repo:

```bash
./scripts/fetch_external_deps.sh
```

Esto intentará colocar:
- `external/jitterentropy`
- `cpujitter-qualifier/external/SP800-90B_EntropyAssessment`

Opciones útiles:

```bash
./scripts/fetch_external_deps.sh --help
./scripts/fetch_external_deps.sh --no-update
./scripts/fetch_external_deps.sh --jent-ref master --nist-ref master
```

> Si no está disponible `jitterentropy.h`, el build puede compilar con backend mock **no productivo** si `CPUJITTER_ENABLE_MOCK_BACKEND=ON`.

---

## 3) Compilar y probar `libcpujitter`

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Si `CPUJITTER_BUILD_PROBE=ON` (por defecto), al compilar `dice_app` se ejecuta un probe y se generan:
- `build/cpujitter_probe_random.bin`
- `build/cpujitter_probe_meta.txt`

---

## 4) Ejecutar ejemplo runtime

```bash
./build/dice_app
```

El ejemplo muestra:
- origen del perfil seleccionado (cache/bundled/recalibrated),
- variables runtime finales,
- entropía por bit estimada,
- estado resumido,
- tiradas de d6 justo (rechazo de valores para evitar modulo bias).

Modo probe (sin tiradas):

```bash
./build/dice_app --probe --dump build/salida.bin --meta build/salida_meta.txt
```

---

## 5) Usar `cpujitter-qualifier` para crear perfiles

Compilar y probar qualifier:

```bash
cmake -S cpujitter-qualifier -B cpujitter-qualifier/build
cmake --build cpujitter-qualifier/build
ctest --test-dir cpujitter-qualifier/build --output-on-failure
```

Ejecutar:

```bash
cd cpujitter-qualifier
./build/cpujitter_qualifier
```

Salida típica:
- `cpujitter-qualifier/reports/sweep_report.txt`
- `cpujitter-qualifier/exported_profiles/selected_profile.json`

Después, curar/validar y añadir el perfil al bundle de runtime (`profiles/` + `profiles/index.json`).

---

## 6) Flujo recomendado de operación

1. Preparar deps externas con `fetch_external_deps.sh`.
2. Compilar/probar runtime.
3. Ejecutar qualifier en plataformas objetivo.
4. Curar perfiles exportados.
5. Publicar runtime con perfiles conocidos.
6. En máquinas nuevas, runtime usa cache/selector/recalibración ligera automáticamente.

---

## 7) Si creo otra app, ¿qué debo hacer para que funcione el jitter?

### Paso A — Dependencias
- Asegura que `libcpujitter` compile en modo productivo con jitterentropy real disponible.
- Si estás en entorno de dev sin jitterentropy, puedes usar mock backend (solo pruebas).

### Paso B — Build de tu app
- Enlaza contra `libcpujitter`.
- Incluye header público:

```c
#include "cpujitter/cpujitter.h"
```

### Paso C — Inicialización
En tu app, define rutas:
- `profiles/index.json` (bundle de perfiles)
- cache local (archivo escribible por el proceso)

Ejemplo:

```c
cpujitter_ctx *ctx = NULL;
cpujitter_err err = cpujitter_init(&ctx, "profiles/index.json", "cache/local_profile.json");
if (err != CPUJITTER_OK) {
    // manejar error
}
```

### Paso D — Obtener aleatorios

```c
uint8_t bytes[32];
err = cpujitter_get_bytes(ctx, bytes, sizeof(bytes));
```

Para d6 justo:

```c
uint8_t die = 0;
err = cpujitter_roll_die(ctx, &die);
```

### Paso E — Inspección runtime (recomendado)

```c
cpujitter_runtime_config cfg;
cpujitter_get_runtime_config(ctx, &cfg);

char status[1024];
size_t written = 0;
cpujitter_get_status_json(ctx, status, sizeof(status), &written);
```

### Paso F — Cierre limpio

```c
cpujitter_shutdown(ctx);
```

---

## 8) Checklist rápido para una app nueva

- [ ] `cpujitter_init` llamado al arrancar.
- [ ] Archivo de cache escribible por el proceso.
- [ ] Manejo explícito de errores (`cpujitter_strerror`).
- [ ] `cpujitter_shutdown` al terminar.
- [ ] Logs/estado (`cpujitter_get_status_json`) para observabilidad.
- [ ] Nunca meter tooling pesado de qualifier en startup de app final.

---

## 9) Notas prácticas

- Mantén runtime y qualifier separados.
- Si cambias esquema de perfiles/cache, sube `schema_version` y documenta migración.
- Usa el qualifier para explorar plataformas nuevas, no para startup normal de usuarios.
