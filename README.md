# EchoEar 2.0 — русскоязычный голосовой ассистент-котик 🐱

Прошивка для клона **EchoEar 2.0** (OSTB / SpotPear, ESP32-S3) на базе проекта
[78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32). Устройство — «умная
колонка с характером»: круглый экран с анимированными глазами кота, голосовое
общение по-русски через xiaozhi.me, сенсорный экран и wake word.

<p align="center"><i>Плата: <code>main/boards/ostb-echoear-2st</code></i></p>

## Что умеет

- 💬 **Общение по-русски** через xiaozhi.me (LLM + ASR + TTS)
- 🗣 **Wake word «София»** — распознаётся на устройстве (ESP-SR WakeNet), звук не покидает устройство, пока кот не разбужен
- 👀 **23 анимации глаз**, из них 12 нарисованы специально для этой прошивки — у каждой эмоции своё лицо ([галерея ниже](#галерея-эмоций))
- 😴 **Живые состояния**: в ожидании кот моргает и скучающе поглядывает вниз; при прослушивании глаза ровно светятся (не мигают); после 2 минут тишины засыпает и гасит подсветку до 5% — будится словом «София», касанием или кнопкой
- 🔎 **MCP-инструменты на борту**: веб-поиск (DuckDuckGo) и погода (Open-Meteo, русские описания) — ассистент может искать и отвечать актуальными данными
- 🔋 Уровень батареи через АЦП, системный светодиод, тач CST816S (тап — начать/остановить диалог)

## Галерея эмоций

Все анимации отрисованы как на устройстве: рисуется левый глаз, правый — зеркальная копия.

### Нарисованные для этой прошивки

| | | |
|:---:|:---:|:---:|
| ![happy](docs/emotions/happy_soft.gif)<br>`happy` — довольная дуга | ![laughing](docs/emotions/laughing.gif)<br>`laughing` — смеётся | ![loving](docs/emotions/loving.gif)<br>`loving` — сердца бьются |
| ![delicious](docs/emotions/delicious.gif)<br>`delicious` — звёзды в глазах | ![silly](docs/emotions/silly.gif)<br>`silly` — googly-глаза | ![confident](docs/emotions/confident.gif)<br>`confident` — самодовольный |
| ![embarrassed](docs/emotions/embarrassed.gif)<br>`embarrassed` — румянец | ![surprised](docs/emotions/surprised.gif)<br>`surprised` — зрачок в точку | ![funny](docs/emotions/funny.gif)<br>`funny` — шарик на орбите |
| ![relaxed](docs/emotions/relaxed.gif)<br>`relaxed` — «дышит» прищур | ![dozing](docs/emotions/dozing.gif)<br>ожидание — моргает, скучает | ![attentive](docs/emotions/attentive.gif)<br>слушает — ровно светит |

### Штатные (Espressif)

| | | |
|:---:|:---:|:---:|
| ![neutral](docs/emotions/neutral.gif)<br>`neutral` — моргает | ![sad](docs/emotions/sad.gif)<br>`sad` | ![cry](docs/emotions/cry.gif)<br>`crying` |
| ![angry](docs/emotions/angry.gif)<br>`angry` | ![shocked](docs/emotions/shocked.gif)<br>`shocked` | ![confused](docs/emotions/confused.gif)<br>`thinking` / `confused` |
| ![sleep](docs/emotions/sleep.gif)<br>`sleepy` + глубокий сон | ![winking](docs/emotions/winking.gif)<br>подмигивание | ![happy-stock](docs/emotions/happy.gif)<br>дуга с сердечками (не используется) |

## Железо

| Узел | Чип | Пины |
|---|---|---|
| SoC | ESP32-S3 N16R8 (16 МБ flash, 8 МБ PSRAM) | |
| Экран | ST77916, круглый 1.85" 360×360, QSPI | data 4-7, clk 3, cs 8, rst 9, подсветка 41 |
| Тач | CST816S (I2C 0x15) | SDA 12, SCL 11, INT 42 |
| Кодек | ES8311 (выход) + ES7210 (микрофоны) | I2S: MCLK 10, BCLK 15, WS 16, DOUT 13, DIN 14, PA 18 |
| Батарея | АЦП GPIO17 (делитель 100К/100К) | |
| LED | GPIO46 (через транзистор) | |

Распиновка добыта из схемы SpotPear и проверена на живом устройстве.

## Сборка

Нужен **ESP-IDF ≥ 5.5.2**:

```bash
idf.py set-target esp32s3
idf.py menuconfig   # Xiaozhi Assistant → Board Type → OSTB EchoEar 2.0
idf.py build
```

Русский язык, стиль эмоций и wake word уже прописаны в
`main/boards/ostb-echoear-2st/config.json`. Wake word меняется в
menuconfig (`ESP Speech Recognition → Wake Word`) — сборка сама
запакует выбранную модель в assets-партицию.

## Прошивка

⚠️ **Не шейте merged-binary на 0x0 при обновлении** — затрёт NVS с настройками Wi-Fi.

Обновление кода и ресурсов:

```bash
python -m esptool --chip esp32s3 -p COM13 -b 921600 write_flash \
    0x20000  build/xiaozhi.bin \
    0x800000 build/expression_assets.bin
```

Первая прошивка чистого устройства — полный образ (`idf.py flash`), дальше — только эти два раздела.

## Свои эмоции

Формат анимаций `.eaf` разобран и реализован на Python — в `scripts/eaf_tools/`:

- `eaf_decode.py` — распаковка `.eaf` в кадры/GIF (RLE, Huffman, палитра)
- `eaf_encode.py` — сборка своих кадров обратно в `.eaf`
- `gen_emotions.py` — генератор всех кастомных анимаций (детерминированный: перезапуск даёт байт-в-байт те же файлы)

Правила рисования: в кадре 125×160 рисуется **только левый глаз** — правый
прошивка достраивает зеркалированием, поэтому буквы и асимметричные жесты
невозможны; контент не должен касаться краёв кадра; прозрачность у энкодера
бинарная — затухание делается яркостью цвета. Маппинг «эмоция → файл» — в
`main/boards/ostb-echoear-2st/assets/360_360/emote.json`.

## Благодарности

- [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) — базовый проект
- Espressif — компоненты `esp_emote_gfx`, `esp_emote_assets`, ESP-SR
- Плата создана по мотивам `esp-vocat` (официальный EchoEar), но без
  `DetectPcbVersion` — у клона другая обвязка I2C

## Лицензия

MIT, как у исходного проекта.
