# Amanita Ocean

Экспериментальный stereo algorithmic reverb для электронной музыки, Psytrance,
Ambient и Downtempo. Текущая версия `0.13.0` — проверяемый DSP-прототип с
собственным масштабируемым интерфейсом на C++20, JUCE 8.0.14 и CMake.

Проект не воспроизводит интерфейс, пресеты, режимы или алгоритмы коммерческих
ревербераторов. Визуальный язык и все графические элементы Amanita Ocean
созданы специально для проекта.

## Что реализовано

- 8-line Feedback Delay Network;
- ортонормальная feedback-матрица Адамара `H8 / sqrt(8)`;
- prime nominal delay lengths, пересчитываемые при смене sample rate;
- два независимых четырёхступенчатых stereo all-pass diffuser;
- единый Character `Default/Bloom/Drift/Veil` с плавным 200-ms morph
  без сброса хвоста;
- Bloom rising-tap swell, второй stereo diffusion layer и intrinsic micro-drift;
- единый Drift, плавно развивающийся от воздушного движения к выраженным
  body/presence trajectories максимального Evolution; оба spectral kernel
  остаются линейными и пассивными в штатном тракте;
- Veil lossless AP6 disperser, превращающий атаки в плотное ~90-ms облако;
- mode-aware Evolution, согласованно управляющий силой Character и движением
  дробных delay lines;
- RT60-derived gain каждой feedback-линии;
- Low Cut и High Damping внутри feedback loop;
- stereo excitation/decoding и M/S Width;
- one-knob Focus с perceptual dry/wet masking-анализом, пассивными
  спектральными pockets, transient-aware слоем и адаптивной stereo geometry;
- плавный Freeze с отключением входа и feedback gain `0.9995`
  (`0.9985` для Bloom); в Drift спектральное вырезание плавно уходит в bypass,
  сохраняя чистый удерживаемый тембр и движение FDN delay lines;
- сглаживание всех непрерывных параметров; для Evolution используется
  150-ms ramp, для смены Character — 200-ms morph;
- защита от NaN/Inf, denormal и аварийной амплитуды;
- сохранение/восстановление текущего состояния через
  `AudioProcessorValueTreeState`;
- собственный векторный JUCE-интерфейс с единым Character selector, центральным
  Evolution, редактируемыми значениями и адаптивным масштабированием;
- CPU-only Deep Current background с mode-aware bathymetric flow, медленными
  световыми полями и плавной остановкой при Freeze;
- keyboard focus, arrow-key навигация Character и accessibility metadata для
  всех интерактивных элементов;
- native targets: VST3/AU/CLAP для macOS arm64 и VST3/CLAP для Windows и
  Linux x64.

Пока не реализованы отдельные low/high RT60 controls, пресеты и финальная
система factory/user preset browser.

## Архитектура

```text
stereo input
    -> smoothed variable pre-delay
    -> L/R all-pass diffusion (4 stages per channel)
       -> Default: direct excitation
       -> Bloom: Evolution blend -> causal rising-tap FIR -> second L/R AP4 diffusion
       -> Veil: Evolution blend -> decorrelated L/R AP6 transient disperser
    -> smoothed Character morph
    -> two orthogonal excitation vectors
    -> 8 Evolution-modulated fractional delay lines (+ Bloom slow per-line drift)
    -> low-cut + high-frequency damping + RT60 gain
    -> Drift: Evolution morph of two linear passive spectral feedback kernels
    -> orthonormal H8 feedback matrix
    -> two orthogonal stereo output projections
    -> wet-only M/S width
    -> wet Focus from dry/wet spectral conflict + transient detector
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
к нулю, а gain ограничен значением `0.9995` или более консервативным `0.9985`
для Bloom.

### Default

Default оставляет базовую FDN без дополнительного excitation или spectral
kernel. `Evolution` управляет только глубиной очень медленного движения
дробных read positions: от `0.025 ms` при 0% до `0.650 ms` при 100% по
smoothstep-кривой. Поэтому минимум не становится полностью статичным, а
максимум заметно оживляет хвост без отдельного chorus-блока.

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

`Evolution` смешивает этот дополнительный excitation path с базовой diffusion:
от `8%` при 0% до `100%` при 100% по smoothstep-кривой. Поэтому минимум остаётся
узнаваемым, но едва заметным, а максимум сохраняет полный Bloom версии `0.6.0`.

Восемь read positions FDN дополнительно движутся с независимыми частотами
`0.013–0.041 Hz`. Внутренняя глубина drift растёт от `0.10` до `0.28 ms` с
индивидуальным scale `0.86–1.15`, а его фактический вклад также умножается на
силу Bloom. В результате минимум начинается примерно с `0.008 ms`, максимум
достигает `0.322 ms`. Во Freeze feedback Bloom ограничен `0.9985` для запаса
устойчивости при переменных дробных задержках.

### Drift

Drift не фильтрует готовый wet-output. После damping, Freeze morph и RT60 gain,
но до Hadamard-матрицы, feedback-вектор проецируется на четыре ортогональные
строки H8: две относятся к левому spectral motion, две — к правому. Поэтому
движение становится частью рекурсивного хвоста и оставляет спектральный imprint
в состоянии FDN.

Единый `DriftCharacter` скрывает два постоянно прогретых linear passive kernel.
На нижнем конце
`Evolution` доминирует медленный воздушный слой с endpoints `600 Hz / 18 kHz`
и периодами примерно `27–73 s`. При 0% его trajectory span равен `0.25`, а
глубина filter morph — `0.15`: хвост живой, но движение остаётся слабым.

По мере роста `Evolution` smoothstep-кривая одновременно увеличивает размах и
плавно переносит вес во второй kernel:

```text
body band     = RBJ band-pass, 360 Hz, Q 0.90
presence band = RBJ band-pass, 3.1 kHz, Q 0.48
```

Его быстрые периоды равны `5.52/10.91 s`, медленные — `47.85/68.03 s`.
При 100% спектральный morph достигает полного high-Evolution endpoint:
trajectory span `1.0`, body/presence depths `0.42/0.58`. Это перенастроенное
Drift 2-ядро, но с общей delay-модуляцией Drift, ограниченной `0.10 ms` ради
чистого саба. Оба результата смешиваются convex-интерполяцией, поэтому переход
не расширяет энергию. Посэмпловых signal-dependent norm guards в штатном тракте
нет; фильтры пассивны по конструкции, а sub не проходит отдельный изменяемый
фильтр.

При Freeze subtractive spectral kernel плавно уходит в bypass вместе с 50-ms
Freeze morph. Уже сформированный тембр хвоста удерживается линейным feedback gain
`0.9995`, а медленное движение fractional delay lines продолжается. Это сохраняет
живое пространство без signal-dependent компенсации и проверяется 120-секундным
Freeze render.

### Veil

Veil размывает атаку без envelope detector, gain pumping, lookahead и
гранулярных окон. После общей входной AP4 diffusion сигнал проходит ещё через
шесть строго устойчивых fixed all-pass ступеней на канал:

```text
L delays, ms: 1.57, 2.53, 3.89, 5.83, 8.47, 12.53
L gains:      0.65, 0.62, 0.59, 0.56, 0.53, 0.50

R delays, ms: 1.67, 2.63, 4.01, 5.89, 8.59, 12.67
R gains:      0.63, 0.60, 0.58, 0.55, 0.52, 0.49
```

В `prepare()` времена пересчитываются в ближайшие нечётные простые длины.
Magnitude response полного AP-каскада остаётся единичным, поэтому стационарный
сигнал не становится просто тише: перераспределяется фаза и групповая задержка
коротких атак. Центр энергии самого AP6 находится около `35 ms`; вместе с
базовой diffusion основная энергия облака укладывается примерно в `90 ms`.
Same-sample составляющая импульса находится около `-50 dB`, но onset остаётся
каузальным — скрытый pre-delay не добавляется.

Veil находится только в excitation path и не увеличивает feedback gain. Его
состояние обрабатывается постоянно даже при выключенном режиме, поэтому
Default/Bloom/Drift↔Veil интерполируются за `200 ms` без холодного старта и
щелчка. `Evolution` смешивает базовое возбуждение и выход AP6 с силой от `4%`
до `100%` по smoothstep-кривой; сам convex blend пассивен и не может усилить
стационарную частоту. AP6 остаётся фиксированным и не создаёт chorus или pitch
wobble.

### Focus (Perceptual Ducking)

`Focus` — один макро-параметр `0–100%`. Его внутренняя технология — perceptual
ducking: начиная с `0.10.0`, отдельный `SpatialDucker` не понижает весь wet-канал
вслед за громкостью dry. Он параллельно
анализирует dry и готовый wet в четырёх широких perceptual bands с центрами
`160 Hz`, `630 Hz`, `2.2 kHz` и `6.5 kHz`. Спектральный pocket появляется только
там, где wet действительно достаточно силён, чтобы маскировать активную область
dry. Если соответствующая wet-полоса уже заметно тише источника, она остаётся
практически нетронутой.

Wet формируется каскадом фиксированных cut-only TPT-SVF stages. Их коэффициенты
не модулируются, latency отсутствует, а при нулевой редукции каждый stage
является точным identity без фазовой окраски. Максимальная локальная глубина
presence-pocket равна примерно `5.5 dB`; low/body/air затрагиваются существенно
меньше. Отдельный onset detector сравнивает быструю и медленную энергию и может
дать только короткое широкополосное освобождение до `1.8 dB`. Поэтому sustained
вокал не воспринимается как постоянная атака и не проваливает весь хвост.

Связь каналов определяется 40-ms анализом balance и positive correlation.
Центральный коррелированный источник получает согласованную обработку Mid, но
большая часть wet Side сохраняется; чистый hard-left или hard-right источник не
изменяет противоположный канал. При уходе источника из центра старая редукция
тихой стороны отпускается быстрой spatial-огибающей. Amount применяется после
постоянно прогретых detector/shaper states и сглаживается за `50 ms`, поэтому
`0%` становится sample-exact bypass. Вся обработка находится после Width и до
Mix, не влияет на feedback, RT60 или содержимое Freeze и остаётся одной ручкой.

DSP находится в `Source/dsp` и не зависит от JUCE/UI. Все буферы создаются в
`prepare()`; `process()` и `processSample()` не выделяют память и не используют
блокировки или системные вызовы.

## Интерфейс

Интерфейс построен вокруг одного главного действия: выбранный Character задаёт
тип движения, а большой центральный `Evolution` — его силу. `Default`, `Bloom`,
`Drift` и `Veil` образуют один взаимоисключающий selector; активный режим
обозначается только тонким нижним подчёркиванием. Никаких скрытых переключателей
режимов нет. Название и значение `Evolution`, как и у остальных ручек,
расположены под регулятором. Остальные восемь непрерывных параметров собраны в
нижнюю полосу, `Freeze` вынесен в заголовок.

Визуальное направление — «bathymetric instrument»: глубокий petrol-black фон,
тёплый светлый текст и медленно движущиеся контурные линии поля. Для каждого
Character используется свой сдержанный accent — бирюзовый, медный, синий или
лиловый. Начиная с `0.11.0`, CPU-only `DeepCurrentRenderer` добавляет три очень
медленных световых поля и пятнадцать дальних контуров. В `0.11.1` поле
расширено тринадцатью сквозными flow-линиями: движение теперь проходит под
header, Character selector, центральной областью и всей нижней полосой ручек.
В `0.11.2` графический кэш переведён в нативное логическое разрешение, а
Evolution изменяет геометрию поля без дискретного появления слоёв через
8-битную прозрачность.
`Default` дышит почти изотропно, `Bloom` расширяет поле, `Drift` создаёт
горизонтальный shear, а `Veil` смягчает и сжимает глубину. Evolution управляет
амплитудой, Freeze примерно за `1.5 s` останавливает течение.

Deep Current рендерится в отдельный полупрозрачный ARGB-кэш `1:1` до
`1200 × 750 px` с качественной интерполяцией при дальнейшем увеличении editor.
Кэш занимает около `2.30 MB` при `960 × 600` и не более `3.60 MB` при
максимальном размере. Во время движения Evolution, смены Character и перехода
Freeze используется `30 FPS`; установившееся автономное течение обновляется на
`15 FPS`. Анимация работает только в editor/message thread, пропускает
обновления скрытого editor и не связана с audio thread.

Базовый размер — `960 × 600 px`, допустимый диапазон — от `800 × 500` до
`1440 × 900` с фиксированным отношением `16:10`. Вся графика рисуется средствами
JUCE, без растровых ресурсов. Числовые значения можно редактировать напрямую;
такие изменения отправляются хосту как завершённые automation gestures.

## Параметры

| Параметр | Диапазон | Назначение |
|---|---:|---|
| Character | Default/Bloom/Drift/Veil | Единственный переключатель алгоритма хвоста |
| Mix | 0–100 % | Линейный dry/wet mix |
| Decay | 0.2–30 s | Broadband RT60 feedback-сети |
| Size | 50–200 % | Масштаб всех FDN delay lengths |
| Pre-delay | 0–250 ms | Задержка возбуждения перед diffusion |
| Low Cut | 20–1000 Hz | Низкочастотное затухание внутри loop |
| High Damping | 1–20 kHz | Cutoff однополюсного damping LPF |
| Evolution | 0–100 % (default 35 %) | Сила и движение выбранного Character: от едва заметного до полного |
| Width | 0–200 % | Ширина только wet-сигнала |
| Focus | 0–100 % (default 100 %) | Выводит dry на передний план через локальные spectral pockets, transient и stereo separation |
| Freeze | Off/On | Плавная фиксация текущего хвоста |

Начиная с `0.11.5`, Decay и Low Cut используют непрерывную экспоненциальную
кривую через прежние midpoint `3 s` и `120 Hz`. Это сохраняет музыкально
полезное распределение диапазона, но убирает чрезмерно длинный ход между
первыми отображаемыми значениями у минимума. В `0.11.6` внутреннее значение
Low Cut остаётся непрерывным, но UI и host-текст всегда отображают его целыми
герцами.

Все четыре варианта Character взаимоисключающие: выбор нового режима полностью
заменяет предыдущий, без дополнительных флагов и скрытых приоритетов. Версия
`0.9.0` добавил предварительный host-ID `ducking`; `0.10.0` заменил исходный
full-band gain ducking на Perceptual Ducking. В `0.10.1` публичный параметр и
host-ID переименованы в `Focus` / `focus`, чтобы описывать слышимый результат,
а не внутреннюю технологию. Проект пока не релизный, поэтому миграция
pre-release state/automation намеренно не гарантируется. После обновления UI хосту может
потребоваться полный rescan или перезапуск, чтобы сбросить кэш editor bundle.

## Требования

- CMake 3.22+;
- C++20 toolchain для выбранной платформы;
- доступ к GitHub при первой конфигурации: JUCE 8.0.14 и
  `clap-juce-extensions` загружаются через `FetchContent` и закреплены на
  commits `2cdfca8feb300fb424002ba2c2751569e5bacb64` и
  `16e9d4ca7b1e86c76e04584b2c08e85a764bcda8` соответственно.

Поддерживаемые native toolchains:

- macOS 11+ на Apple Silicon: Xcode Command Line Tools или Xcode;
- Windows 10/11 x64: Visual Studio 2022 с workload `Desktop development with C++`;
- Linux x86_64: GCC или Clang, Ninja и системные JUCE-зависимости. Для
  Ubuntu 22.04 их можно установить так:

```sh
sudo apt update
sudo apt install build-essential libasound2-dev libfontconfig1-dev libfreetype-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev ninja-build pkg-config \
  xauth xvfb
```

Вместо загрузки можно передать локальный JUCE checkout:

```sh
-DFETCHCONTENT_SOURCE_DIR_JUCE=/absolute/path/to/JUCE
-DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS=/absolute/path/to/clap-juce-extensions
```

## Сборка macOS arm64

Из корня проекта:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DBUILD_TESTING=ON

cmake --build build \
  --target AmanitaOcean_VST3 AmanitaOcean_AU AmanitaOcean_CLAP \
           AmanitaOceanDSPTests AmanitaOceanStateTests \
  --parallel
```

Артефакты находятся здесь:

```text
build/AmanitaOcean_artefacts/Release/VST3/Amanita Ocean.vst3
build/AmanitaOcean_artefacts/Release/AU/Amanita Ocean.component
build/AmanitaOcean_artefacts/Release/CLAP/Amanita Ocean.clap
```

Локальные macOS bundle получают ad-hoc подпись после сборки. Для распространения
её нужно заменить Developer ID подписью в отдельном packaging/notarization
процессе.

## Сборка Windows x64

Из `Developer PowerShell for VS 2022`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_TESTING=ON

cmake --build build --config Release `
  --target AmanitaOcean_VST3 AmanitaOcean_CLAP `
           AmanitaOceanDSPTests AmanitaOceanStateTests `
  --parallel

ctest --test-dir build --build-config Release --output-on-failure
```

Release использует статический MSVC runtime (`/MT`), поэтому готовым плагинам
не требуется отдельная установка Visual C++ Redistributable.

## Сборка Linux x86_64

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON

cmake --build build \
  --target AmanitaOcean_VST3 AmanitaOcean_CLAP \
           AmanitaOceanDSPTests AmanitaOceanStateTests \
  --parallel

xvfb-run --auto-servernum \
  ctest --test-dir build --output-on-failure
```

Windows и Linux создают те же `VST3/` и `CLAP/` пути внутри
`build/AmanitaOcean_artefacts/Release`. На Windows VST3 является bundle-папкой,
а CLAP — отдельным `.clap` файлом; на Linux VST3 также является bundle-папкой,
а CLAP — ELF shared library с расширением `.clap`.

## Установка

Сборка не копирует плагины автоматически. Рекомендуемые пользовательские пути:

| Платформа | VST3 | CLAP | AU |
|---|---|---|---|
| macOS | `~/Library/Audio/Plug-Ins/VST3` | `~/Library/Audio/Plug-Ins/CLAP` | `~/Library/Audio/Plug-Ins/Components` |
| Windows | `%LOCALAPPDATA%\Programs\Common\VST3` | `%LOCALAPPDATA%\Programs\Common\CLAP` | — |
| Linux | `~/.vst3` | `~/.clap` | — |

После копирования Bitwig или другому хосту может потребоваться rescan плагинов.

## Native CI artifacts

Workflow `.github/workflows/build-platforms.yml` собирает Windows x64 на MSVC и
Linux x86_64 на Ubuntu 22.04. В обоих job выполняются DSP/UI tests, CLAP проходит
официальный `clap-validator`, а VST3 проверяется через JUCE manifest helper и
наличие native entry point. Готовые архивы хранятся в GitHub Actions 14 дней:

```text
Amanita-Ocean-<version>-Windows-x64.zip
Amanita-Ocean-<version>-Linux-x64.tar.gz
```

Linux упаковывается в `tar.gz`, чтобы сохранить executable bit. Windows CI
артефакты пока не подписываются Authenticode: это отдельный release-процесс.

CLAP собирается официальным community bridge `clap-juce-extensions` как target
`AmanitaOcean_CLAP`. Постоянный plugin ID — `audio.amanitaocean.reverb`, features
— `audio-effect`, `reverb`, `stereo`; Bitwig получает нативные диапазоны JUCE
параметров вместо нормализованных значений `0…1`.

## Тесты и offline render

```sh
ctest --test-dir build --output-on-failure

clap-validator validate \
  "./build/AmanitaOcean_artefacts/Release/CLAP/Amanita Ocean.clap" \
  --only-failed

./build/AmanitaOceanDSPTests --test-ducking

./build/AmanitaOceanDSPTests \
  --render ./build/amanita_ocean_default_ir.wav

./build/AmanitaOceanDSPTests \
  --render-bloom ./build/amanita_ocean_bloom_ir.wav

./build/AmanitaOceanDSPTests \
  --render-drift ./build/amanita_ocean_drift_ir.wav

./build/AmanitaOceanDSPTests \
  --render-veil ./build/amanita_ocean_veil_ir.wav

./build/AmanitaOceanDSPTests --stress-bloom

./build/AmanitaOceanDSPTests --stress-drift

./build/AmanitaOceanDSPTests --stress-veil

./build/AmanitaOceanStateTests \
  --render-ui ./build/amanita_ocean_ui.png 0 960
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
- плавные morph между четырьмя Character без сброса хвоста;
- автоматизацию Evolution `0↔100%` во всех четырёх режимах без щелчков;
- stereo decorrelation и детерминированную эволюцию Bloom;
- оба линейных passive kernel единого Drift, их convex morph и sub bypass;
- суперпозицию Drift и полностью включённого Drift Freeze;
- отсутствие чрезмерной генерации 6–10 kHz в хвосте вокалоподобного сигнала,
  ограниченного 5.8 kHz, при Evolution `30%/100%` и в Freeze;
- независимые L/R spectral trajectories и in-loop imprint Drift;
- сохранение энергии, L/R decorrelation и sample-rate timing AP6 Veil;
- настоящее размытие импульса Veil: leading energy, crest, centroid и NRMS;
- повторяющийся kick+bass pattern при 190 BPM с четырёхполосным анализом;
- Perceptual Ducking при centered/hard-left/hard-right входе на всех sample
  rate: band selectivity, реальный dry/wet masking conflict и sample-exact 0%;
- вокалоподобный Drift render и 190-BPM kick+bass: локальную разборчивость без
  общего wet-collapse, коротких pumping holes и долгого imprint на хвосте;
- точную независимость противоположного канала, сохранение Side центрального
  источника, Freeze isolation и полное восстановление внутреннего FDN state;
- детерминированные fingerprints минимального и максимального Evolution
  каждого Character;
- round-trip текущего state и точный host→DSP routing четырёх Character,
  Evolution и Focus;
- создание custom editor, APVTS attachments, resize limits `800–1440 px`,
  доступность всех controls, непрерывный rotary travel Decay/Low Cut без
  стартовой ступени и корректный host gesture при вводе чисел;
- детерминированность Deep Current, движение и заметность в верхней, нижней,
  левой и правой четвертях editor, нативное разрешение кэша, плавный
  покадровый Evolution без одиночных скачков, resize cache cycle и
  Freeze-to-idle timing;
- независимость результата от размера audio block;
- отсутствие C++ allocations во время обработки.

`--stress-bloom` дополнительно выполняет 90-секундный Freeze render при
максимальных Size/Decay/Evolution. Он покрывает полный цикл самого медленного
Bloom LFO (~77 секунд) и проверяет энергию по десятисекундным окнам.
`--stress-drift` делает аналогичный 120-секундный render, перекрывая все Drift
LFO periods обоих внутренних kernels. `--stress-veil` выполняет 90-секундную
проверку AP6 excitation и Freeze с нижней и верхней границами энергии хвоста.

В контрольном 190-BPM kick+bass render рост Evolution единого Drift от 0% до
100% увеличил non-sub spectral motion с `0.0422` до `0.0928`, а stereo spectral
motion — с `0.0508` до `0.1190`; symmetric NRMS равен `0.389`. Slow sub motion
остаётся малым (`0.0351→0.0897 dB`), sub late/early ratio равен `1.013`, поэтому
низ не накапливается от такта к такту.

В версии `0.8.3` Drift больше не использует посэмпловую нормализацию энергии
stateful filters. Full-FDN superposition NRMS равен `7.23e-7`; в тесте
вокалоподобного сигнала с верхней гармоникой 5.8 kHz доля 6–10 kHz составляет
`−102.1 dB` при Evolution `30%` и `−104.6 dB` при `100%`. В полностью включённом
Freeze superposition NRMS равен `9.19e-7`, доля 6–10 kHz — `−101.0 dB`, а
late/early energy ratio — `0.592`.

При Veil 100% доля энергии первых `12 ms` после onset снизилась с `6.83%` до
`0.056%`, crest — с `6.93` до `3.93`, а centroid переместился с `50.1` до
`72.5 ms` при отношении полной энергии `0.961`. На повторяющемся kick+bass при
190 BPM high-frequency peak равен `0.627x` Default, полная энергия — `0.986x`,
средняя sub energy — `0.995x`; symmetric NRMS равен `1.57`.

Контрольные Release DSP renders используют Evolution `35%`. В версии `0.8.3`
Drift fingerprint изменился из-за линейной passive topology; fingerprints
Default, Bloom и Veil остаются прежними:

```text
Default  SHA-256 66afecc4b8614f49a76c4788cdb92ae41a13b4f721ecab6432c7a0849f910c14
Bloom    SHA-256 bb20c28ac650ae555a2f3fc0a09a47808c7b09099b3f924330157ea22123233b
Drift    SHA-256 742888b64fdd76650582e81cabed898629af71c5f8a8c56d32a6c4d3b52c3935
Veil     SHA-256 406546d8be3a1bf231561c9cae3129db3875557e6d85bf741272ca92cd4c4f47
```

Для дополнительной проверки памяти:

```sh
cmake -S . -B build-sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DBUILD_TESTING=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"

cmake --build build-sanitize \
  --target AmanitaOceanDSPTests AmanitaOceanStateTests \
  --parallel

ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
ctest --test-dir build-sanitize --output-on-failure
```

`detect_leaks=0` нужен потому, что bundled Apple AddressSanitizer не поддерживает
LeakSanitizer на этой платформе.
