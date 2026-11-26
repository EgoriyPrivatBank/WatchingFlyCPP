# WatchingFlyCPP v1.0.0

Дата: 2026-11-26

## Что это

WatchingFlyCPP — симуляция мозга мухи на реальном коннектоме FlyWire/Princeton
(166 700 нейронов, 6 242 118 связей, 89 860 280 синапсов). Видео с YouTube или
локальный файл проходит через модель глаза, сигнал идёт по настоящей проводке,
а на выходе считаются попытки побега, удовлетворённость и поведение. В live-режиме
открывается браузер с картой нейронов, графиками и видео.

## Что нового

- Первый публичный релиз: готовый Windows x64-пакет, компилятор не нужен.
- Portable-сборка `WatchingFlyCPP.exe`; только системные DLL Windows.
- `start_live.bat` (YouTube), `start_file.bat` (локальное видео),
  `setup_tools.ps1` (ffmpeg/yt-dlp с проверкой SHA256), `check_tools.bat`.
- Live-страница: видео, карта 166 700 нейронов, графики, журнал побегов.

## Требования

- Windows 10 или 11, **64-bit**.
- ~1 ГБ свободного места: данные (~232 МБ), ffmpeg (~205 МБ загрузка),
  распаковка, кэш `connectome.bin` (~79 МБ), видео.
- Интернет — только для YouTube.
- Компилятор не нужен.

## Быстрый старт (5 шагов)

1. Скачайте `WatchingFlyCPP-v1.0.0-win64.zip` и `SHA256SUMS.txt`.
2. Проверьте архив:
   `certutil -hashfile WatchingFlyCPP-v1.0.0-win64.zip SHA256` — хеш должен
   совпасть со строкой в `SHA256SUMS.txt`.
3. Распакуйте в любую папку (права администратора не нужны).
4. Скачайте данные FlyWire (public release v783) через
   [FlyWire Codex](https://codex.flywire.ai/) и положите в `data\`:
   `neurons.csv` и `connections_princeton.csv`. Лицензия данных —
   [CC BY-NC 4.0](https://flywire.ai/guidelines), только некоммерческое
   использование с атрибуцией.
5. Запустите `setup_tools.ps1` (правый клик → Run with PowerShell; при запрете
   политики: `powershell -ExecutionPolicy Bypass -File setup_tools.ps1`),
   затем `check_tools.bat`, затем `start_live.bat` или перетащите видео на
   `start_file.bat`.

Первый запуск строит `connectome.bin` (~30 с) и открывает
`http://127.0.0.1:8765/`. Скорость — примерно **1.0–1.2×** реального времени.

## Известные ограничения

- Только Windows (Winsock, winmm); Linux/macOS не поддерживаются.
- exe не подписан: SmartScreen/антивирус может предупреждать. Это ожидаемо;
  проверьте SHA256 перед запуском.
- Первый запуск строит кэш (~30 с).
- Portable-сборка на очень длинных (400 с) прогонах может отличаться от native
  в последнем печатном знаке CSV; 20-с регрессия точная.
- Данные FlyWire в архив не входят; без них live-режим не запустится.
- Оценки «удовлетворённости» и «поведения» — прокси-индексы, не измерение эмоций.

## Данные и лицензии

- Код WatchingFlyCPP: **MIT**, см. `LICENSE` в архиве.
- Данные FlyWire/Princeton: **CC BY-NC 4.0**, отдельно от кода; атрибуция
  Dorkenwald et al. (2024), doi:10.1038/s41586-024-07558-y, Schlegel et al.
  (2024), doi:10.1038/s41586-024-07686-5, FlyWire Consortium.
- ffmpeg: GPL-сборка BtbN, скачивается отдельно по желанию пользователя.
- yt-dlp: Unlicense, скачивается отдельно.

## Файлы релиза

- `WatchingFlyCPP-v1.0.0-win64.zip` — пакет для Windows x64.
- `SHA256SUMS.txt` — SHA256 архива и `WatchingFlyCPP.exe`.

Репозиторий: https://github.com/EgoriyPrivatBank/WatchingFlyCPP
