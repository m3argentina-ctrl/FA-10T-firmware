# PROMPT — Generar Manual de Usuario del Controlador Control ESP32-S3 v3.0

> Copiá TODO lo que sigue (desde "Tarea" hasta el final) y pegalo en una nueva
> conversación con Claude. El manual generado debería ser de ~15 a 30 páginas
> en Markdown, listo para convertir a PDF.

---

## Tarea

Sos un redactor técnico especializado en manuales de usuario para equipos
industriales. Generá un **Manual de Usuario completo y autocontenido en
Markdown** para el controlador electrónico **Control ESP32-S3 v3.0** de la empresa
argentina **Bio Origen** (`www.bioorigen.com.ar`, `info@bioorigen.com.ar`).

El destinatario es el **operador del equipo** (no técnico ni electrónico).
Debe poder leer este manual sin conocer programación, electrónica ni
ingeniería de control. Usa lenguaje claro, frases cortas, y muchos ejemplos
paso a paso.

El producto es un **controlador térmico industrial para deshidratadores de
alimentos** (modelo de referencia `IND-26MTO`). Controla las resistencias
calefactoras, las turbinas tangenciales y un extractor auxiliar mediante
relés de estado sólido (SSR). Tiene una pantalla táctil color 3.5" como
única interfaz de usuario, además de feedback acústico (clicks al tocar).

## Contexto del producto

- **Fabricante**: Bio Origen — alimentos deshidratados.
- **Modelo del controlador**: Control ESP32-S3 v3.0 (Firmware versión 3 fase 4).
- **Modelo de equipo por defecto**: IND-26MTO (editable desde Área Técnica).
- **Hardware**:
  - Display: pantalla táctil color 480×320 con touch capacitivo.
  - Audio: parlante interno con clicks suaves al tocar la pantalla.
  - Sensores: PT1000 (temperatura), SHT31 (humedad ambiente).
  - Salidas: 3 SSR (resistencia calefactora, turbinas tangenciales,
    extractor auxiliar).
  - Seguridad: temperatura máxima 85°C, detección de fuga térmica,
    monitoreo de corriente de turbinas.
- **Rango de operación**: 20°C a 80°C.
- **Modos**:
  - **MODO MANUAL**: el operador fija una temperatura y un tiempo total.
  - **MODO PROGRAMAS**: 6 programas guardables con **3 etapas** cada uno
    (cada etapa = temperatura + duración propias).

## Estructura visual: layout general

Todas las pantallas operativas (excepto SPLASH) tienen:

- **Panel izquierdo blanco** (120 px de ancho) con:
  - El **isologo** de Bio Origen arriba (la "b" verde con flor naranja).
  - 5 botones de navegación verticales: **INICIO** (gris), **MANUAL**
    (naranja), **PROGRAMAS** (verde), **TECNICA** (azul), **ALARMAS**
    (rojo). Un pequeño triángulo verde indica la pantalla activa.
- **Panel derecho oscuro** (360 px) con el contenido de la pantalla.

## Pantallas — descripción detallada

### 1. SPLASH (pantalla de arranque)

- Fondo blanco completo.
- Logo grande "**bioOrigen** — *volver a lo natural...*" centrado arriba.
- Línea con "MODELO" y el nombre del modelo (ej. "IND-26MTO") en naranja.
- URL del fabricante.
- Barra de progreso naranja que se llena durante ~5 segundos.
- Al completarse pasa automáticamente a **INICIO**.

### 2. INICIO (menú principal)

- Panel derecho:
  - Texto "bioOrigen" grande naranja como título.
  - "MODELO" + nombre del modelo en naranja.
  - "MODOS DE OPERACION" como subtítulo.
  - **Dos botones grandes**:
    - **MANUAL** (naranja, mitad izquierda) → abre el programador manual.
    - **PROGRAMAS** (verde, mitad derecha) → abre la lista de programas.

### 3. PROGRAMACIÓN MANUAL (configurar modo manual)

- Título "PROGRAMACION MODO MANUAL" en naranja arriba.
- **TEMPERATURA °C**: par de botones grandes [−] [+] con el valor entre
  ellos en rojo grande (rango 20–80 °C, paso 0.1°C). Mantener apretado
  acelera de a 1°C, después 5°C, etc.
- **TIEMPO HH:MM**: par de botones [−] [+] con el valor entre ellos en
  azul grande (rango 00:01 a 99:59, paso 1 minuto, luego 5, luego 10,
  luego 30).
- Botón **INICIAR** (naranja, grande, abajo) → arranca el ciclo y pasa a
  la pantalla de operación manual.

### 4. OPERACIÓN MANUAL (ciclo corriendo)

Pantalla durante un ciclo manual. Se actualiza cada 0.5 s.

Layout de arriba abajo:

1. **Barra de estado** (verde / naranja / amarillo / verde) con el texto
   actual: "FUNCIONANDO", "CALENTANDO", "PAUSADO" o "COMPLETADO".
2. **Fila de 4 cajitas** (de izq. a der.):
   - **SET POINT** (borde naranja) — temperatura objetivo °C.
   - **T PROGRAM.** (borde azul) — tiempo total programado (HH:MM:SS).
   - **RESISTENCIA** (fondo cambia entre **verde "ON"** y **rojo "OFF"**)
     — estado del SSR de la resistencia calefactora en este instante.
   - **TURBINAS** (mismo formato verde/rojo) — estado del ventilador.
3. **TEMPERATURA actual** en grande (rojo) + "/ 60 °C" (referencia al
   setpoint en gris).
4. **Barra horizontal roja** que muestra la temperatura actual sobre
   un rango 0–100°C.
5. **TIEMPO restante** en grande (azul, formato HH:MM:SS) + "/ HH:MM:SS
   total" (referencia al tiempo total).
   - **Si el equipo todavía está calentando** (T < SP), en lugar del
     tiempo se muestra **"CALENTANDO"** en amarillo y abajo "esperando
     setpoint". El tiempo NO empieza a descender hasta que la
     temperatura alcanza el setpoint.
6. **Barra horizontal azul** del tiempo (0% al iniciar, 100% al
   completar).
7. **Fila de 2 cajitas** abajo:
   - **T MIN SESION** (borde cyan) — temperatura mínima registrada
     durante esta sesión.
   - **T MAX SESION** (borde amarillo) — temperatura máxima.
8. **3 botones grandes** abajo:
   - **PAUSAR** (naranja) — pausa el ciclo. La resistencia se apaga, el
     contador se congela. Vuelve a ser azul si está pausado y dice
     "REINICIAR" en su lugar.
   - **REINICIAR** (azul) — reanuda un ciclo pausado.
   - **DETENER** (rojo) — termina el ciclo y vuelve al programador
     manual.

### 5. PROGRAMAS — Lista de programas (selección)

Grilla **3 columnas × 2 filas** con 6 botones grandes (PROG 1 a PROG 6).

- **Slot vacío** (gris): muestra "PROG N" y debajo "vacio". Al tocar
  va directo al wizard de edición.
- **Slot guardado** (verde): muestra "PROG N", debajo el **nombre que
  le puso el operador** (ej. "HIERBA 1", "MANZANA"), y al pie el
  **tiempo total** del programa (HH:MM). Al tocar **abre un modal de
  decisión** con 3 botones:
  - **EDITAR** (azul) — abre el wizard de edición cargado con los
    valores actuales del programa.
  - **INICIAR** (verde) — arranca directamente el programa y va a la
    pantalla de operación.
  - **CANCELAR** (gris) — cierra el modal y vuelve a la grilla.

### 6. WIZARD DE EDICIÓN DE PROGRAMA — Etapa 1/2/3

Tres pantallas idénticas en formato (una por etapa). El layout es muy
similar a la pantalla de programación manual:

- Título "PROG N — ETAPA X/3" en verde.
- Bloque **TEMPERATURA °C** con [−] [valor grande] [+].
- Bloque **TIEMPO HH:MM** con [−] [valor grande] [+].
- Abajo:
  - **← ATRÁS** (gris) — vuelve a la etapa anterior; en la etapa 1
    vuelve a la lista de programas.
  - **SIGUIENTE →** (verde) — pasa a la etapa siguiente. En la etapa 3
    el botón se llama **REVISAR →** (naranja) y va a la pantalla de
    resumen.

### 7. WIZARD DE EDICIÓN — Pantalla de Resumen / Grabar

- Título "RESUMEN — slot N" en verde.
- Lista de las 3 etapas con su temperatura (rojo) y duración (azul).
- "TOTAL: HH:MM" en cyan.
- Campo **NOMBRE** (default: "PROG N") con un botón **EDITAR** azul al
  lado. Al tocar cualquiera de los dos se abre un **teclado modal
  alfanumérico** a pantalla completa con:
  - Textarea grande arriba mostrando lo que se tipea.
  - Letras A-Z, dígitos 0-9, símbolos `_ - .`, espacio (SP), backspace
    (⌫), CANCELAR y OK.
- 3 botones abajo:
  - **← ATRÁS** (gris) — vuelve a la etapa 3.
  - **GRABAR** (rojo) — guarda el programa en memoria persistente y
    vuelve a la lista de programas.
  - **INICIAR** (naranja) — guarda y arranca el ciclo de inmediato.

### 8. OPERACIÓN PROGRAMAS (ciclo de programa corriendo)

Muy similar a OPERACIÓN MANUAL. Diferencias:

1. **Barra superior verde** que muestra "**`<nombre>` — ETAPA X/3**" (o
   "CALENTANDO `<nombre>`" mientras precalienta, o "PAUSADO ..."  ,
   "COMPLETADO ...").
2. **Fila de 3 cajas ETAPA** (en lugar de las 4 cajitas del manual).
   Cada caja muestra "ETAPA N" arriba, temperatura en rojo abajo-izq y
   duración en azul abajo-der. **La etapa activa se ilumina en naranja**
   con borde amarillo. Cuando avanza a la siguiente etapa, la caja
   nueva se ilumina y la anterior vuelve a fondo oscuro.
3. Temperatura grande + barra roja igual que MANUAL.
4. Tiempo grande + barra azul igual que MANUAL (también muestra
   "CALENTANDO" mientras precalienta).
5. **Fila de 2 cajitas abajo**: RESISTENCIA | TURBINAS (verde ON / rojo
   OFF, igual que en MANUAL).
6. Mismos 3 botones: **PAUSAR / REINICIAR / DETENER**.

> **Importante**: el tiempo de proceso (la cuenta atrás del programa
> total) NO empieza hasta que la temperatura alcanza el setpoint de la
> primera etapa. Esto asegura que el operador obtenga el tiempo real de
> proceso útil, no incluyendo el calentamiento inicial.

### 9. ÁREA TÉCNICA (acceso protegido por PIN)

**Modal de PIN al ingresar** (full-screen, fondo oscuro):
- Título "INGRESE PIN" en azul.
- Campo grande con borde azul que muestra los dígitos como asteriscos.
- Teclado numérico 3×3 (1-9 + 0 + ⌫ + OK) + botón **CANCELAR** (rojo)
  ancho debajo, que vuelve a INICIO sin autenticar.
- PIN por defecto: **1234** (modificable desde la misma pantalla).

**Pantalla Técnica autenticada**:
- Título "AREA TECNICA" en azul.
- **Fila 1 de 4 cajitas**: HRS TOTAL, CICLOS SSR, DESDE SVC (cambia de
  blanco a amarillo con icono ⚠ después de 450 hs), FALLA FAN.
- **Fila 2 de 4 cajitas**: Kp / Ki (ganancias del PID), I NOM
  (corriente nominal aprendida), T MAX HIST (temperatura máxima
  histórica), SESIONES (cantidad de sesiones completadas).
- **Log de eventos** (3 últimas entradas) con colores:
  - Verde + "+" para arranques de sesión.
  - Rojo + "!" para fallas.
  - Naranja + "~" para cortes de energía.
- Campo **MODELO** editable (tap abre el teclado alfanumérico modal).
- **Fila de 5 botones abajo**:
  - **SALIR** (rojo) — vuelve a INICIO y desloguea.
  - **AUTOTUNE** (azul) — calibración automática del PID (stub).
  - **CALIBRAR** (naranja) — ajuste fino de la calibración de
    temperatura.
  - **RESET SVC** (verde) — resetea el contador de horas desde el
    último servicio.
  - **CAMBIAR PIN** (violeta) — abre 2 modales numéricos secuenciales:
    primero "NUEVO PIN" (1-4 dígitos), luego "CONFIRMAR PIN NUEVO". Si
    coinciden, el PIN se guarda en memoria persistente.

### 10. ALARMA

Pantalla que aparece automáticamente si se dispara una falla de
seguridad (sobre-temperatura, fuga térmica, falla de turbinas, sensor
roto). Muestra el tipo de falla, la corriente real de turbinas y la
nominal aprendida, y un botón para reconocer la alarma y volver al
estado normal una vez que la condición desaparezca.

## Feedback acústico

Cada toque a la pantalla genera un click corto y suave (~2 kHz, 25 ms)
para confirmación táctil. El operador escucha el feedback al tocar
botones, teclas, cualquier zona activa.

## Identidad persistente

Estos valores se guardan en memoria no volátil y sobreviven al apagado:

- **Modelo del equipo**: editable desde AREA TECNICA.
- **PIN de servicio**: 4 dígitos, default 1234, modificable desde AREA
  TECNICA.
- **Programas 1 a 6**: cada uno con nombre, 3 etapas (T + duración) y
  tiempo total.
- **Historial**: horas totales, ciclos de SSR, T máxima histórica,
  cantidad de sesiones completadas.

## Flujos típicos de uso

Generá secciones paso-a-paso para cada uno de estos casos:

### Caso 1: Deshidratar manzanas en modo MANUAL a 55°C durante 8 horas
1. Encender el equipo.
2. Esperar el splash y llegar a INICIO.
3. Tocar **MANUAL**.
4. Tocar **+** o **−** de temperatura hasta llegar a 55°C.
5. Tocar **+** de tiempo manteniendo apretado hasta llegar a 08:00.
6. Tocar **INICIAR**.
7. La pantalla pasa a operación con barra naranja "CALENTANDO".
8. Cuando alcanza 55°C, la barra cambia a verde "FUNCIONANDO" y el
   tiempo empieza a descender desde 08:00:00.
9. Al cumplir las 8 horas la pantalla muestra "COMPLETADO" y el
   equipo se apaga solo.
10. Tocar **DETENER** para volver al programador.

### Caso 2: Crear un programa de 3 etapas para hierbas aromáticas
1. INICIO → **PROGRAMAS**.
2. Tocar un slot vacío (gris) — por ejemplo PROG 2.
3. En la **ETAPA 1**: setear T = 45°C, tiempo = 02:00. Tocar
   **SIGUIENTE →**.
4. En la **ETAPA 2**: setear T = 50°C, tiempo = 04:00. Tocar
   **SIGUIENTE →**.
5. En la **ETAPA 3**: setear T = 40°C, tiempo = 02:00. Tocar
   **REVISAR →**.
6. En el RESUMEN, tocar **EDITAR** al lado del nombre. Tipear
   "HIERBAS" en el teclado y tocar **OK**.
7. Tocar **GRABAR** — el programa queda guardado en el slot 2 y se
   vuelve a la lista (slot 2 ahora aparece en verde con el nombre).

### Caso 3: Ejecutar un programa ya guardado
1. INICIO → **PROGRAMAS**.
2. Tocar el slot guardado (verde, con nombre).
3. En el modal, tocar **INICIAR** (verde).
4. La pantalla pasa a operación con "CALENTANDO `<nombre>`".

### Caso 4: Pausar y reanudar un ciclo
1. Durante el ciclo, tocar **PAUSAR**.
2. La resistencia y turbinas se apagan, el tiempo se congela.
3. Tocar **REINICIAR** para reanudar.

### Caso 5: Cambiar el PIN de servicio
1. INICIO → **TECNICA**.
2. Ingresar el PIN actual (default 1234) y tocar **OK**.
3. Tocar **CAMBIAR PIN**.
4. Tipear el PIN nuevo (1-4 dígitos) y tocar **OK**.
5. Confirmar el mismo PIN y tocar **OK**.
6. Tocar **SALIR** para volver a INICIO.

### Caso 6: Cambiar el modelo del equipo (ABM)
1. INICIO → **TECNICA** → autenticar.
2. Tocar el campo **MODELO**.
3. En el teclado modal, borrar y tipear el nombre nuevo.
4. Tocar **OK** — el nuevo modelo aparece en la pantalla SPLASH y en
   INICIO.

## Estados y colores

Resumen para incluir en el manual:

| Estado | Color barra | Texto barra | Significado |
|--------|------------|-------------|-------------|
| Idle | (no se muestra) | — | Sin sesión activa |
| Calentando | Naranja | CALENTANDO | Ciclo iniciado pero T < setpoint |
| Funcionando | Verde | FUNCIONANDO | T alcanzó setpoint, tiempo corriendo |
| Pausado | Amarillo | PAUSADO | Operador pausó manualmente |
| Completado | Verde | COMPLETADO | Tiempo total cumplido, ciclo terminó |
| Alarma | Rojo (pantalla ALARMA) | (variable) | Falla de seguridad |

| Cajita RESISTENCIA / TURBINAS | Color fondo | Texto |
|--------|-------------|-------|
| Encendida | Verde | ON |
| Apagada | Rojo | OFF |

## Solución a problemas comunes

Generá una sección con al menos estos casos:

- **El equipo se queda en CALENTANDO**: la temperatura no alcanza el
  setpoint. Posibles causas: puerta abierta, resistencia floja,
  ambiente muy frío.
- **El equipo entró en ALARMA**: identificar el tipo de alarma en
  pantalla, ver cómo limpiar el latch.
- **Olvidé el PIN**: hay que conectar el equipo al servicio técnico para
  resetearlo.
- **Pantalla no responde al tacto**: instrucciones de limpieza, no usar
  guantes mojados, no apretar muy fuerte.
- **El tiempo nunca empieza a descender**: explicar que el tiempo NO
  arranca hasta llegar al setpoint, es comportamiento normal.

## Formato del manual generado

Quiero el manual con **estas secciones, en este orden**:

1. **Portada**: Nombre del producto, modelo, versión de firmware (3.0),
   logo (referencia: "/assets/logo.png"), versión del manual (1.0),
   fecha (mes actual / año actual).
2. **Tabla de contenidos** (con anclas a cada sección).
3. **Introducción** (1-2 páginas): qué es el Control ESP32-S3, para qué sirve, a
   quién está dirigido este manual, símbolos usados (⚠ peligro, 💡
   tip, ℹ nota).
4. **Primeros pasos**: cómo encender, qué se ve en el SPLASH, llegar a
   INICIO.
5. **Descripción de la interfaz**: el menú izquierdo, la barra de
   estado, cómo navegar entre pantallas.
6. **Modo MANUAL** (1-2 páginas): programación y operación, con captura
   esquemática de la pantalla (descripta en ASCII art si no podés
   generar imágenes).
7. **Modo PROGRAMAS** (3-4 páginas): lista, modal de decisión, wizard
   de edición etapa por etapa, resumen, ejecución.
8. **Área Técnica** (2 páginas): autenticación, telemetría disponible,
   acciones (autotune, calibrar, reset svc, cambiar PIN), edición del
   modelo.
9. **Alarmas y seguridad** (1-2 páginas): tipos de alarma, cómo
   reconocer, cómo limpiar.
10. **Casos de uso típicos**: los 6 escenarios listados arriba como
    procedimientos paso a paso.
11. **Solución de problemas** (FAQ).
12. **Glosario** (PID, SSR, SP, etapa, deshidratado).
13. **Especificaciones técnicas resumidas** (rango T, capacidad
    programas, etc.).
14. **Contacto del fabricante**.

## Tono y estilo (importante)

- **Lenguaje claro y directo**. Frases cortas. Voz activa.
- **Instrucciones en imperativo**: "Tocar", "Esperar", "Verificar".
- Cada paso de procedimiento debe ser **una acción concreta** numerada.
- Usa **negritas** para nombres de botones (**INICIAR**, **PAUSAR**) y
  para etiquetas de pantalla (**SET POINT**, **T PROGRAM.**).
- Usa **`código`** para valores fijos como tiempos, temperaturas, PIN.
- **No uses jerga técnica** sin explicarla (p.ej. "PID" debe definirse
  en el glosario; "SSR" debe explicarse como "relé de estado sólido,
  un componente que enciende y apaga la resistencia").
- Cuando describas una pantalla, podés usar **ASCII art** simple o
  tablas para representar el layout visual. No es necesario que sean
  imágenes reales.
- Idioma: **español rioplatense** (vos, no tú). El audience es
  argentino.
- Longitud objetivo: 15–30 páginas equivalentes en PDF.
- Markdown bien formado, listo para convertir a PDF con `pandoc`.

## Output

Generá el manual completo en un solo bloque Markdown, sin truncar.
Empezá con `# Manual de Usuario — Controlador Control ESP32-S3 v3.0` y terminá con
la sección de contacto. No agregues comentarios fuera del manual.
