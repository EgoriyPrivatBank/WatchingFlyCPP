# Proposal

## Why

Код flysim опубликован, но пользователь без компилятора не может просто скачать и
запустить проект. Нужен готовый Windows-пакет (portable exe + скрипты + данные по
инструкции) и GitHub Release с ZIP и контрольными суммами, без изменения модели.

## What Changes

- Portable-сборка `flysim_portable.exe` из текущих исходников, проверка
  зависимостей и быстрая регрессия.
- Папка `out\release\WatchingFlyCPP-v1.0.0-win64\`: exe, `start_live.bat`,
  `start_file.bat`, `setup_tools.ps1`, `check_tools.bat`, `data\`, `tools\`,
  `README_RU.txt`, `LICENSE`, `CHANGELOG.txt`, `SHA256SUMS.txt`.
- Данные FlyWire по умолчанию **не включаются**; README/скрипт объясняют, куда
  положить CSV.
- ZIP + SHA256; GitHub Release создаётся **draft**, публикация — только по
  команде владельца.
- Документы релиза: `RELEASE_NOTES.md`, раздел «Скачать» в README.

## Capabilities

Поведение модели не меняется; только упаковка и релиз. `skip_specs: true`.

### New Capabilities

Нет.

### Modified Capabilities

Нет.

## Impact

- Новые файлы — только упаковка/документация; `src\`, формулы и CSV-поведение не
  меняются.
- В git не коммитятся exe, ZIP, CSV, `connectome.bin`, `out\release\`.
- Релизные ассеты: ZIP и `SHA256SUMS.txt` (прикладываются к Release).
