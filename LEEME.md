# KeyShift

Plugin VST3 (efecto) para Windows. Se pone en la pista donde suena el sample y:

1. **Detecta la tonalidad** (mayor/menor) y la muestra en la pantalla, con confianza y afinación en cents.
2. En **percusión tonal** (kicks, 808, bajos) muestra la **nota exacta** (p. ej. G#1).
3. En percusión sin altura (cajas, hats) lo indica; solo permite transportar por octavas/cents.
4. Cambia el sample a la **tonalidad mayor o menor que elijas**.

## Cómo conseguir el .vst3

No hace falta instalar nada:

1. Crea un repositorio en GitHub y sube todo el contenido de esta carpeta.
2. Pestaña **Actions** → "Compilar KeyShift (VST3 Windows)" (arranca solo al subir).
3. Cuando termine (unos 10 min), descarga el artefacto **KeyShift-VST3-Windows**.
4. Copia la carpeta `KeyShift.vst3` a `C:\Program Files\Common Files\VST3` y re-escanea plugins en tu DAW.

(Alternativa local: Visual Studio 2022 + CMake → `cmake -S . -B build` y `cmake --build build --config Release --target KeyShift_VST3`.)

## Uso

- Inserta KeyShift en la pista del sample y reprodúcelo. Se analiza solo; la pantalla muestra el resultado.
- Elige la **tonalidad destino** (12 notas + Mayor/Menor) y activa **APLICAR CAMBIO**.
- **Detección automática** desactivada → puedes indicar tú la tonalidad de origen si el análisis falla.
- **Tipo de sonido**: Auto / Tonal / Percusión-una nota (fuerza el modo si se equivoca).
- **Motor**: Tonal (vocoder, mejor para melodías/acordes) o Percusivo (granular, conserva el golpe de kicks/cajas). Auto elige solo.
- **Octava** y **Ajuste fino** para retoques. **Autoafinar** corrige la desafinación del sample.
- **Exportar WAV**: renderiza el último sample con el cambio aplicado. Modo *Mantener duración* o *Estilo vinilo* (tono y velocidad cambian a la vez). Luego puedes arrastrar el WAV a tu DAW.

## Notas

- Latencia fija de 3072 muestras (el DAW la compensa solo).
- Cómo decide el cambio: se transporta por el camino más corto (máx. ±6 semitonos); la octava la ajustas tú.
- Mayor y su relativa menor comparten notas (Do mayor = La menor): pasar de La menor a Do mayor es 0 st.
- Procesado de audio: JUCE 8 (licencia AGPLv3/comercial; consulta si vas a distribuir el plugin).
