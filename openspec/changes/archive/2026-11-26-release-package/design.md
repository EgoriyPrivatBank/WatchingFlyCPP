# Design

## Версия и данные

- Версия: **v1.0.0** (предложение; подтверждает владелец).
- Данные FlyWire: официальная страница <https://flywire.ai/guidelines> —
  **CC BY-NC 4.0**, атрибуция, некоммерческое использование.
- По умолчанию **вариант A**: CSV не входят в архив; `data\README.txt` и
  `README_RU.txt` дают ссылку и инструкцию. Вариант B (данные отдельным ассетом)
  — только по решению владельца.

## Сборка

- `build.bat portable` → `flysim_portable.exe` (без `-march=native`).
- Проверка: 0 предупреждений, только системные DLL (kernel32/ws2_32/winmm и
  т.п.), нет libstdc++/libgcc/libwinpthread.
- Регрессия: validate, eyetest (2 побега), `--shuffle` (0), 20-с seed 42 = эталон.
  Известное расхождение portable на 400-с прогоне — в заметках релиза.

## Пакет

```
flysim.exe              portable, переименован
start_live.bat          URL -> --live run --url ... --dir data
start_file.bat          перетащить файл -> --video ... --dir data
setup_tools.ps1         официальные ffmpeg/yt-dlp -> tools\, с подтверждением
check_tools.bat         версии инструментов + наличие CSV
data\README.txt         что положить в data\
tools\                  пустая, setup кладёт сюда
README_RU.txt           простая инструкция
LICENSE                 MIT
CHANGELOG.txt
SHA256SUMS.txt
```

Скрипты используют `%~dp0`, кавычки, относительные пути; `FLYSIM_FFMPEG` и
`FLYSIM_YTDLP` указывают на `tools\`, данные — `--dir data`.
`setup_tools.ps1` без администратора ставит в `tools\`, качает только с
официальных URL, проверяет опубликованный хеш, если он есть, и спрашивает
подтверждение.

## Проверки

- R4: чистая папка `%TEMP%`, копия пакета, CSV в `data\`, check_tools,
  setup_tools (с разрешения), локальное видео 15 с, YouTube 15 с, live на
  127.0.0.1, report.html, путь с пробелом/кириллицей.
- R6: ZIP, SHA256 архива и exe, распаковка и сверка.
- R7: draft Release, скачивание ассетов и сверка SHA256.

## Ограничения

- Windows-only; интернет для YouTube; первый запуск строит кэш.
- exe не подписан: SmartScreen может предупреждать; проверка через SHA256.
- В git не попадают exe/ZIP/данные; ZIP прикладывается к Release.
