# Журнал `release-package`

## 2026-11-26 — R0 (исходное состояние)

- `git status --short --branch`: чисто, `main...origin/main`.
- `git log`: HEAD `4dc2023` (Make repository public), всего 7 коммитов.
- Remote: `https://github.com/EgoriyPrivatBank/fly-connectome-sim.git`.
- `gh auth status`: `EgoriyPrivatBank`, scopes `gist`, `read:org`, `repo`,
  `workflow`; репозиторий **PUBLIC**, default branch `main`.
- Предпосылки на месте: `build.bat`, `out\baseline\metrics_seed42_20s_threads16.csv`,
  `out\baseline\metrics_seed42_400s_threads16.csv`, оба CSV в `..\neyron\`,
  ffmpeg и yt-dlp в `..\tools\`.
- Версия предложена: **v1.0.0**; ждёт подтверждения.
- README/PROGRESS: Windows 10/11 x64, MIT, данные CC BY-NC 4.0, скорость
  ~1.0–1.2×, portable на 400 с расходится в последнем знаке.

## 2026-11-26 — R1 (данные)

- Официальная страница: <https://flywire.ai/guidelines> — public release
  **CC BY-NC 4.0**, атрибуция, некоммерческое использование.
- Вариант **A (рекомендуется)**: CSV не включать; в `data\README.txt` и
  `README_RU.txt` — ссылка и инструкция, куда положить файлы.
- Вариант **B**: приложить данные отдельным ассетом релиза — только по
  подтверждению владельца; учитывать CC BY-NC 4.0 и размер ~232 МБ.
- Пока ответа нет — действует A.

### Ответы владельца (2026-11-26)
1. Версия **v1.0.0** подтверждена.
2. Данные: **A** — не включать.

### Переименование
- По команде владельца GitHub-репозиторий переименован:
  `fly-connectome-sim` → **`WatchingFlyCPP`**
  (https://github.com/EgoriyPrivatBank/WatchingFlyCPP).
- `origin` обновлён; старый URL редиректит.
- Пакет/архив/заголовок релиза: `WatchingFlyCPP-v1.0.0`.
- Исходники и CLI-имя `flysim` не меняются; в пакете exe переименовывается
  в `WatchingFlyCPP.exe` (копия portable-сборки).

## 2026-11-26 — R2 (portable-сборка)

- `build.bat portable` → `flysim_portable.exe`, BUILD OK, 0 предупреждений.
- Размер: **3 304 097 байт** (~3.15 MiB).
- `objdump -p`: только `KERNEL32.dll`, UCRT (`api-ms-win-crt-*`), `WINMM.dll`,
  `WS2_32.dll`; нет `libstdc++`, `libgcc`, `libwinpthread`.
- `build.bat` для portable: `ARCH=` (без `-march=native`).
- Регрессия portable:
  - validate: 166 700 / 9 039 608 / 6 242 118 / 89 860 280;
  - eyetest: 2 побега, GF looming 0.056, rest 0.004;
  - `--shuffle eyetest`: 0 побегов, GF looming 0.018;
  - 20-с seed 42: CSV SHA256 = эталон (`7938A65C…`).
- Скорость короткого прогона в этот момент: 0.84× (нагрузка машины); CSV
  идентичен. В аудите было 1.0–1.2×; в заметках релиза — «~1.0–1.2×».
- Известное расхождение portable на 400-с прогоне (последний знак) остаётся
  задокументированным.

## 2026-11-26 — R3 (структура пакета)

- Создана папка `out\release\WatchingFlyCPP-v1.0.0-win64\`.
- `WatchingFlyCPP.exe` — копия `flysim_portable.exe` (3 304 097 байт).
- `LICENSE`, `CHANGELOG.txt`, `README_RU.txt`, `data\README.txt`.
- `start_live.bat`, `start_file.bat`, `check_tools.bat` — тесты без данных:
  корректные сообщения, exit 1, окно не закрывается.
- `setup_tools.ps1`: parse errors 0; не запускался.
  - ffmpeg: BtbN `latest` GPL zip, официальная ссылка с ffmpeg.org;
    проверка по `checksums.sha256` (текущий хеш zip `0e838e…`).
  - yt-dlp: 2026.11.23, `SHA2-256SUMS` (текущий хеш `622ed4…`).
  - подтверждение перед скачиванием, установка в `tools\`, без админа,
    временные файлы только внутри пакета и удаляются.
- `tools\` пока пустая; `SHA256SUMS.txt` и `RELEASE_NOTES.md` — на R5/R6.
- Владелец согласовал текст `setup_tools.ps1` и разрешил запуск в R4.

## 2026-11-26 — R4 (тест нового пользователя)

### Установка
- Пакет скопирован в `%TEMP%\flysim_user_test`, CSV — в `data\`.
- `setup_tools.ps1 -Yes`: 95.4 с, скачано ~229 МБ.
- Установлено: ffmpeg **N-129288-gc81e35cf2b-20261125**, yt-dlp **2026.11.23**;
  SHA256 сверены с официальными файлами.
- `check_tools.bat`: до установки — MISSING, после — все компоненты OK.

### Локальное видео 15 с (`start_file.bat`)
- 453 кадра, 15.3 с, 1.0×; 0 побегов; report.html и metrics_run.csv созданы.
- Live: `/stats` threads=16, active=1000; `/frame.bin` 166 709 байт,
  ненулевых 126 833; `/layout.bin` 3 128 480 байт; HTML содержит LIVE.
- `/video`: HEAD 200 `Content-Length: 1309150`; Range `bytes=0-99` → 206, 100 байт.

### YouTube 15 с
- `WatchingFlyCPP.exe --live run --url ... --max-sec 15`: 450 кадров, 0 побегов,
  15.0 с, 29.4 с всего; `out/ytvideo.mp4` 583 237 байт, report 81 279 байт,
  metrics 77 004 байта.

### Путь с пробелом и кириллицей
- Папка `%TEMP%\flysim тест 路径`, видео `тест видео.mp4`.
- Первый запуск со старыми `%~dp0`/`%~1` упал: приложение не нашло ffmpeg
  (ANSI/Unicode env path) → 0 кадров.
- Исправление только в упаковке: `start_live.bat`/`start_file.bat` используют
  `%~sdp0` и `%~s1` (8.3-короткие пути). После этого 5-с прогон: 152 кадра,
  report.html, 153 строки CSV.
- `start_live.bat` получил то же исправление; YouTube в Unicode-папке отдельно
  не перезапускался (тот же механизм путей).

### Прочее
- Промптов брандмауэра не наблюдалось; сервер слушает 127.0.0.1.
- Временная папка удалена (825 МБ), CSV/exe в ней не остались.

### Бэклог
- Прямой запуск exe с длинным Unicode-путём в `FLYSIM_FFMPEG`/`--video`
  по-прежнему не работает (narrow ANSI/UTF-8 в коде). Для пользователей это
  закрыто 8.3-путями в start-скриптах; менять код в этой задаче нельзя.

## 2026-11-26 — R6 (архив и SHA256)

- ZIP: `out\release\WatchingFlyCPP-v1.0.0-win64.zip`, **0.74 МБ**.
- SHA256 ZIP: `cd19e53904a7155f922c62ac3aa8cba04152b2c0bf571d3584da523181689bc3`.
- SHA256 exe: `ac124cdc0be17460738bd3b547ed234ee2e813f5bacc473a5ae5d00aa567f78a`.
- `SHA256SUMS.txt`:
  - в пакете — только exe (хеш архива нельзя положить внутрь самого архива);
  - в `out\release\` — отдельный файл с ZIP + exe для релиза.
- ZIP содержит верхнюю папку `WatchingFlyCPP-v1.0.0-win64\`, включая пустые
  `data\`/`tools\`.
- Распаковка во `%TEMP%`: хеш exe совпал, `tools\` на месте, CSV/bin/log/mp4 —
  0. Временная папка удалена.

### R7 — план (ждёт подтверждения)
- Тег: `v1.0.0` (annotated) на коммит после записи R6 в журнал.
- Заголовок: `WatchingFlyCPP v1.0.0`.
- Notes: `RELEASE_NOTES.md`.
- Ассеты: `WatchingFlyCPP-v1.0.0-win64.zip` (0.74 МБ), `SHA256SUMS.txt`.
- `gh release create ... --draft`; public release — только по отдельной команде.

## 2026-11-26 — R7 (draft-релиз)

- Коммит `e1049e4` (Record release archive checksums), запушен.
- Tag **v1.0.0** (annotated) на `e1049e4`, запушен.
- Draft Release создан:
  `https://github.com/EgoriyPrivatBank/WatchingFlyCPP/releases/tag/untagged-7efacc7edc73528eb813`
  (после публикации — `/releases/tag/v1.0.0`).
- Ассеты:
  - `WatchingFlyCPP-v1.0.0-win64.zip` — 770 735 байт,
    SHA256 `cd19e539…689bc3`;
  - `SHA256SUMS.txt` — 185 байт.
- `gh release view`: `isDraft: true`; asset digest zip =
  `sha256:cd19e539…689bc3`.
- Скачивание ассетов из draft: zip 770 735 байт, SHA256 совпал; обе строки
  `SHA256SUMS.txt` верны. Временная папка удалена.
- Публикация: **не выполнялась на этапе R7**; позже опубликован по команде
  владельца (см. R8).

## 2026-11-26 — R8 (итог)

- Change `release-package` архивирован; задачи закрыты.
- Итог: релиз v1.0.0 опубликован, пакет 0.74 МБ, проверен на чистой папке.
- **2026-11-26: draft опубликован по команде владельца** (`isDraft=false`):
  https://github.com/EgoriyPrivatBank/WatchingFlyCPP/releases/tag/v1.0.0
  — страница и ZIP отдают HTTP 200; SHA256 публичного ZIP совпал.
- Осталось владельцу: при желании подписать exe; данные FlyWire не включены,
  лицензия CC BY-NC 4.0.
