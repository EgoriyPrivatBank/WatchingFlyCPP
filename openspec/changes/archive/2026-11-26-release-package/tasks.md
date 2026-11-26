# Tasks

## R0. Исходное состояние
- [x] R0.1 `git status`, ветка main, remote origin.
- [x] R0.2 `git log` и версия v1.0.0 (предложение).
- [x] R0.3 `gh auth status` и видимость репозитория.
- [x] R0.4 Сверить факты с README/PROGRESS.
- [x] R0.5 Версия v1.0.0 подтверждена.

## R1. Данные
- [x] R1.1 Проверить официальную лицензию FlyWire.
- [x] R1.2 Предложить варианты A (не включать) и B (отдельный ассет).
- [x] R1.3 Владелец выбрал A: данные не включаем.

## R2. Portable-сборка
- [x] R2.1 `build.bat portable`, 0 предупреждений.
- [x] R2.2 Только системные DLL; `-march=native` не используется.
- [x] R2.3 Регрессия portable: validate/eyetest/shuffle/20 с — OK.
- [x] R2.4 Размер exe: 3 304 097 байт.

## R3. Пакет
- [x] R3.1 Собрать структуру `out\release\WatchingFlyCPP-v1.0.0-win64\`.
- [x] R3.2 Написать `start_live.bat`, `start_file.bat`, `check_tools.bat`.
- [x] R3.3 Написать `setup_tools.ps1` (официальные URL, хеши, подтверждение).
- [x] R3.4 README_RU, data\README, LICENSE, CHANGELOG; SHA256SUMS — позже.
- [x] R3.5 Владелец согласовал `setup_tools.ps1` и разрешил запуск.

## R4. Тест нового пользователя
- [x] R4.1 Копия пакета в `%TEMP%` без исходников/tools.
- [x] R4.2 CSV в data\, check_tools, setup_tools (скачано 229 МБ, 95 с).
- [x] R4.3 Локальное видео 15 с и YouTube 15 с.
- [x] R4.4 Live-страница, `/video` Range, карта (nonzero), report.html.
- [x] R4.5 Путь с пробелом и кириллицей (после перехода на 8.3-пути).
- [x] R4.6 Временная папка удалена (825 МБ).

## R5. Документация релиза
- [x] R5.1 RELEASE_NOTES.md.
- [x] R5.2 Раздел «Скачать» в README; URL обновлён на WatchingFlyCPP.
- [x] R5.3 Показать `git diff --stat`; получить подтверждение commit/push.

## R6. Архив и SHA256
- [x] R6.1 ZIP `WatchingFlyCPP-v1.0.0-win64.zip` (0.74 МБ).
- [x] R6.2 SHA256 архива и exe, `SHA256SUMS.txt` (в пакете и отдельно).
- [x] R6.3 Распаковать и сверить хеш exe — совпал; размер показан.

## R7. GitHub Release
- [x] R7.1 Тег, заголовок, notes, ассеты и размеры показаны.
- [x] R7.2 `v1.0.0` annotated, запушен.
- [x] R7.3 Draft-релиз создан.
- [x] R7.4 Ассеты скачаны, SHA256 совпали.
- [x] R7.5 Draft; публикация не выполнялась (ждёт команды владельца).

## R8. Итог
- [x] R8.1 Change закрыт, итог выдан.
