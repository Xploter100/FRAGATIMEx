# FRAGATIME

Plugin de efectos granular (VST3) para FL Studio, inspirado en Fragments.
Carga un `.wav` / `.mp3` (arrastrándolo o con el botón) y lo transformas en granos.
Si no cargas archivo, granula la señal que entra al canal en vivo.

## Controles (0–100 %)
| Control | Qué hace |
|---|---|
| Intensidad | Mezcla seco/efecto |
| Density | Granos por segundo |
| Size | Duración de cada grano (15–600 ms) |
| Pitch | 50 % = original, ±12 semitonos |
| Arpegiador | 0 = off; sube = más notas (escala pentatónica menor) |
| Granulado | Aleatoriedad de posición, pitch, panorama y tiempo |
| Feedback | Realimentación del bus de granos |
| Posición | Punto de lectura dentro del archivo |
| Efecto base + FX Amount | Ninguno, Reverb, Delay, Bitcrusher, Chorus |

## Descargar el plugin ya compilado
1. Sube este proyecto a GitHub.
2. Pestaña **Actions** → último "Build FRAGATIME" → **Artifacts** → `FRAGATIME-win64-vst3`.
3. (Opcional) crea un tag `v1.0.0` y se publica en **Releases**:
   `git tag v1.0.0 && git push origin v1.0.0`

## Instalar en FL Studio (Windows 11 x64)
1. Copia la carpeta `FRAGATIME.vst3` a `C:\Program Files\Common Files\VST3\`
2. FL Studio → Options → Manage plugins → **Find installed plugins**.
3. En el Mixer, pon FRAGATIME en un slot de efecto (Insert).

## Compilar tú mismo
Requisitos: Visual Studio 2022 (carga "Desarrollo para escritorio con C++"), CMake 3.22+ y Git.
```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Resultado: `build/FRAGATIME_artefacts/Release/VST3/FRAGATIME.vst3`
(también se genera una versión Standalone para probar sin FL Studio).

## Notas de rendimiento (8 GB de RAM)
Máximo 96 granos simultáneos, archivos limitados a 5 min (~100 MB) y buffer en vivo de 8 s.

## Licencia
FRAGATIME usa [JUCE](https://juce.com) (AGPLv3 / licencia comercial). Si lo publicas, mantén el
repositorio abierto bajo AGPL-3.0 (agrega un archivo `LICENSE` desde GitHub → Add file → Create new file → LICENSE → plantilla).
