# Карта проекта flysim

Дата: 2026-11-26. Источник: `src\` (20 файлов), `README.md`, `PROGRESS.md`,
`graphify-out\GRAPH_REPORT.md` (563 узла, 1000 связей, 30 сообществ).

## 1. Модули

| Файл | Роль |
|---|---|
| `src\csv.h` | Быстрый CSV-парсер: поля-спаны, кавычки, BOM, CRLF; `read_file`. |
| `src\connectome.h/.cpp` | Загрузка `neurons.csv` и `connections_princeton.csv`, интернирование строк, схлопывание связей по `(pre,post)`, CSR по постсинаптическим, группы чтения, `connectome.bin`, `--shuffle`. |
| `src\sim.h/.cpp` | `Brain`: rate-модель (используется) и LIF; 16 потоков, шаг 5 мс; клампы фоторецепторов, `ext` ламины, лезия. |
| `src\flyeye.h/.cpp` | Глаз: сетка 32×24, адаптация τ=3.5 с, транзиенты, EMD; `drive_pure` (фото+ламина), `drive_lc` (отладка). |
| `src\eyetest.cpp` | Синтетика 16 с: покой/лунинг/отступление/мерцание/решётка; печать GF/LC4/побегов. |
| `src\selftest.cpp` | Сравнение rate и LIF на ступеньке/мерцании/пандусе. |
| `src\metrics.h/.cpp` | Побег (DNp01), удовлетворённость (DA+MBON−OCT−SER), поведение, CSV 24 колонки, `detect_escapes`. |
| `src\video.h/.cpp` | `yt-dlp`-загрузка, ffmpeg-пайп (gray 320×180@30), `browser.mp4`, статус загрузки. |
| `src\live.h/.cpp` | HTTP-сервер 127.0.0.1, layout/статистика/видео/управление, `publish`. |
| `src\live_page.h` | Встроенная HTML/JS-страница live/replay. |
| `src\run.cpp` | `run_video` (видео→глаз→мозг→метрики→CSV→отчёт), `run_replay`. |
| `src\report.h/.cpp` | Автономный `out\report.html` из CSV. |
| `src\main.cpp` | CLI, загрузка коннектома, диспетчеризация команд. |

## 2. Конвейер данных

```
URL YouTube
  └─ youtube_download()  [yt-dlp.exe, _popen, прогресс]
       └─ out/ytvideo.mp4
            └─ VideoSource::open()  [ffmpeg.exe, _popen]
                 └─ raw gray 320x180 @ 30 fps
                      └─ FlyEye::process_frame()  32x24 омматидиев,
                         адаптация, транзиент, EMD
                           ├─ drive_pure(): clamp фоторецепторов + ext ламины
                           └─ Brain::step() × 6  (rate, dt=5 мс, 16 потоков)
                                └─ Metrics::update()  GF, LC4, satisfied, behavior
                                     ├─ out/metrics_run.csv (24 колонки)
                                     └─ out/report.html
```

Live-ответвление: `LiveServer::publish()` ~20 Гц копирует `rate` (166 700 байт),
глаз (3×768), топ-20 и JSON; страница тянет `/stats`, `/frame.bin`, `/eye.bin`,
`/layout.bin`, `/groups`, `/pipeline`, `/video` и шлёт `POST /ctl`.

Replay: `init_replay_csv()` → `/stats?i=N` отдаёт строку CSV в том же формате.

## 3. Потоки

- Главный поток — симуляция (`run_video`), он же читает пайп ffmpeg и пайп
  yt-dlp. Отдельного потока загрузки нет: `youtube_download()` блокирует
  главный поток, HTTP-сервер в это время жив.
- `Brain::run_parallel()` создаёт и присоединяет до `g_threads` потоков на
  **каждый** шаг (для rate — 6 раз на кадр).
- `LiveServer::start()` — поток `accept_loop`; на каждое соединение —
  отдельный `detach`-поток `conn_loop`.
- ffmpeg/yt-dlp — подпроцессы ОС; их stdout читает главный поток.

## 4. Синхронизация

- `LiveServer::Imp::mtx` — опубликованное состояние (`stats`, `activity`,
  `eye_*`, `top20`, `groups`, `pipeline`, `video_path`).
- `publish()` — `try_lock`; при занятом мьютексе кадр отбрасывается,
  симуляция не ждёт UI.
- `ctl_mtx` + `url_cv_` — `lesion`, `new_url`; `take_new_url_wait()` будит
  главный поток при новой ссылке/рестарте.
- Атомики: `pause_`, `speed_`, `restart_`, `running_`.
- `video.cpp`: `g_dl_mtx` + `g_dl` — статус загрузки.
- В ядре мьютексов нет: потоки пишут непересекающиеся диапазоны нейронов.
- Коды `run_video`: `0` — успех, `1` — ошибка, `2` — рестарт, `3` — новая ссылка.

## 5. Глобальное состояние

`g_threads` и `g_seed` (`sim.cpp`), `g_escape_refractory` (`metrics.cpp`),
`g_new_url_taken` (`run.cpp`), `g_dl`/`g_dl_mtx` (`video.cpp`), статические
таблицы групп/строк в `live.cpp`.

## 6. CLI

```
flysim validate
flysim selftest
flysim eyetest [lc]
flysim run --url <link> | --video <file> [--max-sec N] [--threads N]
           [--seed N] [--escape-refractory S] [--shuffle] [--rebuild]
           [--dir <data-dir>] [--cache <file>] [--live [--port N]]
           [--realtime | --speed 0.5|1|2|max]
flysim replay <metrics.csv> --video <file> [--port N]
flysim report [--video <csv>]
```

## 7. Хранилище

- Вход: `..\neyron\neurons.csv`, `..\neyron\connections_princeton.csv`
  (fallback — текущая папка).
- Кэш: `connectome.bin` в рабочей папке; 166 700 нейронов, 6 242 118 связей.
- Выход: `out\metrics_run.csv`, `out\report.html`, `out\ytvideo.mp4`,
  `out\browser.mp4`, тестовые CSV.

## 8. Наблюдения для аудита (черновик)

- `publish()` читает `im.lesion` без `ctl_mtx` — гонка `std::string`.
- `Brain::run_parallel()` создаёт потоки на каждом шаге — накладные расходы.
- В `graphify-out` попал `out\audit_backup\stage05\main.cpp` — дубль `main()`
  (скан-корень включал бэкап); на карту модулей не влияет.
- `live_page.h` и `report.h` не дали символов для AST — JS/HTML учтён вручную.
