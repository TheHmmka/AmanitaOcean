# Amanita Ocean

Экспериментальный stereo algorithmic reverb для электронной музыки, Psytrance,
Ambient и Downtempo. Текущая версия `0.2.0` — проверяемый DSP-прототип на C++20,
JUCE 8.0.14 и CMake.

Проект не воспроизводит интерфейс, пресеты, режимы или алгоритмы коммерческих
ревербераторов. На первом этапе используется стандартный JUCE Generic Editor.

## Что реализовано

- 8-line Feedback Delay Network;
- ортонормальная feedback-матрица Адамара `H8 / sqrt(8)`;
- prime nominal delay lengths, пересчитываемые при смене sample rate;
- два независимых четырёхступенчатых stereo all-pass diffuser;
- Character `Default/Bloom` с плавным 200-ms morph без сброса хвоста;
- Bloom rising-tap swell, второй stereo diffusion layer и intrinsic micro-drift;
- дробное чтение delay lines и медленная разнесённая модуляция;
- RT60-derived gain каждой feedback-линии;
- Low Cut и High Damping внутри feedback loop;
- stereo excitation/decoding и M/S Width;
- плавный Freeze с отключением входа и feedback gain `0.9995`;
- сглаживание всех непрерывных параметров;
- защита от NaN/Inf, denormal и аварийной амплитуды;
- сохранение/восстановление состояния через `AudioProcessorValueTreeState`;
- VST3 и Audio Unit targets для macOS.

Пока не реализованы следующие Characters (`Drift`, `Veil`), Ducking, Evolution
macro, отдельные low/high RT60 controls, пресеты и собственный графический
интерфейс.

## Архитектура

```text
stereo input
    -> smoothed variable pre-delay
    -> L/R all-pass diffusion (4 stages per channel)
       -> Default: direct excitation
       -> Bloom: causal rising-tap FIR -> second L/R AP4 diffusion
    -> smoothed Character morph
    -> two orthogonal excitation vectors
    -> 8 modulated fractional delay lines (+ Bloom slow per-line drift)
    -> low-cut + high-frequency damping + RT60 gain
    -> orthonormal H8 feedback matrix
    -> two orthogonal stereo output projections
    -> wet-only M/S width
    -> smoothed dry/wet mix
```

Базовые длины при 48 kHz:

```text
1423, 1601, 1871, 2089, 2393, 2687, 3011, 3449 samples
```

Это простые числа, соответствующие примерно `29.6–71.9 ms`. В `prepare()` они
масштабируются под sample rate и заменяются ближайшими нечётными простыми.
Автоматизация Size не выполняет prime snapping: она плавно масштабирует дробные
read positions. Это исключает скачки между соседними простыми длинами в audio
thread.

Для обычного режима gain линии вычисляется из требуемого RT60:

```text
g_i = exp(log(0.001) * delaySeconds_i / decaySeconds)
```

Матрица ортонормальна, loop-фильтры пассивны, а feedback gain всегда меньше
единицы. Во Freeze damping плавно обходится, входное возбуждение стремится точно
к нулю, а gain ограничен значением `0.9995`.

### Bloom

Bloom создаёт reverse-like вспухания без reverse playback, гранулярных окон,
lookahead и дополнительной plugin latency. После базовой diffusion используется
каузальный FIR-кластер:

```text
s[n] = 0.12*x[n]
     + 0.10*x[n-t1]
     + 0.16*x[n-t2]
     + 0.25*x[n-t3]
     + 0.37*x[n-t4]
```

Тапы постепенно усиливаются и создают восходящую огибающую отражений. Для L
используются времена `7.9, 18.7, 34.1, 55.7 ms`, для R —
`9.1, 21.1, 37.3, 60.1 ms`. Их абсолютные gains суммируются в единицу, после
чего отдельный четырёхступенчатый all-pass слой размывает taps в облако.

Восемь read positions FDN дополнительно движутся с независимыми частотами
`0.013–0.041 Hz`. Глубина intrinsic drift составляет примерно `0.10 ms` при
Modulation 0% и плавно увеличивается до `0.32 ms`; поэтому нулевое значение
ручки убирает пользовательскую модуляцию, но не врождённое дыхание Bloom. Во
Freeze feedback Bloom ограничен `0.9985` для запаса устойчивости при переменных
дробных задержках.

DSP находится в `Source/dsp` и не зависит от JUCE/UI. Все буферы создаются в
`prepare()`; `process()` и `processSample()` не выделяют память и не используют
блокировки или системные вызовы.

## Параметры

| Параметр | Диапазон | Назначение |
|---|---:|---|
| Character | Default/Bloom | Выбор характера хвоста |
| Mix | 0–100 % | Линейный dry/wet mix |
| Decay | 0.2–30 s | Broadband RT60 feedback-сети |
| Size | 50–200 % | Масштаб всех FDN delay lengths |
| Pre-delay | 0–250 ms | Задержка возбуждения перед diffusion |
| Low Cut | 20–1000 Hz | Низкочастотное затухание внутри loop |
| High Damping | 1–20 kHz | Cutoff однополюсного damping LPF |
| Modulation | 0–100 % | Глубина основной delay modulation и добавка к Bloom drift |
| Width | 0–200 % | Ширина только wet-сигнала |
| Freeze | Off/On | Плавная фиксация текущего хвоста |

## Требования

- macOS на Apple Silicon;
- Xcode Command Line Tools или Xcode;
- CMake 3.22+;
- Ninja рекомендуется, но не обязателен;
- доступ к GitHub при первой конфигурации: JUCE 8.0.14 загружается через
  `FetchContent` и закреплён на commit
  `2cdfca8feb300fb424002ba2c2751569e5bacb64`.

Вместо загрузки можно передать локальный JUCE checkout:

```sh
-DFETCHCONTENT_SOURCE_DIR_JUCE=/absolute/path/to/JUCE
```

## Сборка

Из корня проекта:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DBUILD_TESTING=ON

cmake --build build \
  --target AmanitaOcean_VST3 AmanitaOcean_AU AmanitaOceanDSPTests \
  --parallel
```

Артефакты находятся здесь:

```text
build/AmanitaOcean_artefacts/Release/VST3/Amanita Ocean.vst3
build/AmanitaOcean_artefacts/Release/AU/Amanita Ocean.component
```

Плагины намеренно не копируются в `~/Library/Audio/Plug-Ins`. Для проверки в DAW
их можно скопировать вручную после сборки. Локальные bundle получают финальную
ad-hoc подпись после генерации VST3 manifest; для распространения её нужно
заменить Developer ID подписью в отдельном packaging/notarization процессе.

## Тесты и offline render

```sh
ctest --test-dir build --output-on-failure

./build/AmanitaOceanDSPTests \
  --render ./build/amanita_ocean_default_ir.wav

./build/AmanitaOceanDSPTests \
  --render-bloom ./build/amanita_ocean_bloom_ir.wav

./build/AmanitaOceanDSPTests --stress-bloom
```

Offline utility создаёт 10-секундный stereo Float32 WAV при 48 kHz. Тестовый
набор проверяет:

- сохранение энергии feedback-матрицей;
- prime/distinct delay geometry;
- impulse response и отсутствие NaN/Inf;
- 44.1, 48, 88.2 и 96 kHz;
- decay и долговременную устойчивость Freeze;
- восстановление после NaN/Inf на входе;
- резкие изменения всех параметров;
- полный morph Default↔Bloom и частые переключения Character;
- stereo decorrelation и детерминированную эволюцию Bloom;
- независимость результата от размера audio block;
- отсутствие C++ allocations во время обработки.

`--stress-bloom` дополнительно выполняет 90-секундный Freeze render при
максимальных Size/Decay/Modulation. Он покрывает полный цикл самого медленного
Bloom LFO (~77 секунд) и проверяет энергию по десятисекундным окнам.

Release Default render остаётся побитово равен версии до Bloom:

```text
SHA-256 20fd135c915bebb254c8caf7f75007b7e0c8eca8b60f227b6f6df6a169c5dabc
```

Для дополнительной проверки памяти:

```sh
cmake -S . -B build-sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DBUILD_TESTING=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"

cmake --build build-sanitize --target AmanitaOceanDSPTests --parallel

ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
ctest --test-dir build-sanitize --output-on-failure
```

`detect_leaks=0` нужен потому, что bundled Apple AddressSanitizer не поддерживает
LeakSanitizer на этой платформе.
