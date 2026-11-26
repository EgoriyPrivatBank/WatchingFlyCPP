# Журнал `publish-to-github`

## 2026-11-26 — G0 (окружение)

### Проверено
- `git --version`: **2.56.1.windows.1** (`C:\Program Files\Git\cmd\git.exe`).
- `gh`: **не установлен** (`where gh` пуст, типовые пути пусты).
- `winget`: **1.30.310** доступен.
- `git config user.name` / `user.email`: **не заданы** (глобально и локально).
- Git-репозитория нет: `git rev-parse --is-inside-work-tree` → exit 128.
- `.gitignore` и `.gitattributes` отсутствуют.
- `gh auth status` выполнить нельзя — CLI нет.

### OpenSpec
- Создан change `publish-to-github`, `skip_specs: true` (код не меняется).

### Требуется от владельца
1. Разрешение на установку GitHub CLI:
   `winget install --id GitHub.cli --exact`.
2. Имя и email для Git (предложение: GitHub noreply
   `<ID>+<login>@users.noreply.github.com`); нужен GitHub login.
3. Решение по `openspec/`: публиковать журнал аудита в репозитории или
   исключить (по умолчанию — спросить).

### Статус
Остановлено после G0 по правилу «стоп после каждого этапа».

## 2026-11-26 — G1 (.gitignore / .gitattributes)

### Что сделано
- Создан `.gitignore`: exe/o/obj/lib/bin/log, все `*.csv`, `neyron/`, `tools/`,
  бэкап, `out/`, видео, `graphify-out/`, `.vscode/`, секреты/временные файлы.
- Создан `.gitattributes`: LF для исходников/доков/ps1, CRLF для `*.bat`/`*.cmd`,
  binary для exe/bin/картинок/видео.
- `git init -b main` выполнен досрочно (G4.1), чтобы проверить игнор; коммитов нет.

### Проверка
- `git check-ignore -v`: `connectome.bin`, оба CSV, `flysim.exe`, `out/*`,
  `graphify-out/*` игнорируются нужными правилами.
- `src/`, `README.md`, `PROJECT_MAP.md`, `build.bat`, `check.bat`, `check.ps1`,
  `openspec/`, `.gitignore`, `.gitattributes` — не игнорируются.
- `git status --short --ignored`: 10 untracked (`.gitignore`, `.gitattributes`,
  `PROGRESS.md`, `PROJECT_MAP.md`, `README.md`, `build.bat`, `check.bat`,
  `check.ps1`, `openspec/`, `src/`), 22 ignored entries (данные, exe, out, логи).
- Код не менялся.

### Решение по `openspec/`
Включён в репозиторий (рекомендация: виден журнал аудита). Если владелец против,
до первого коммита добавим `openspec/` в `.gitignore`.

## 2026-11-26 — G2 (безопасность и размер)

### Что проверено
- Кандидатов: **42 файла**, суммарно **0.28 МБ**; файлов >50 МБ — **0**.
- Поиск `C:\Users\`, `E:\vibecoded`, `E:\project-fly`, `E:\tools`, `Yehor`,
  `password|token|secret|api_key`: реальных секретов/личных данных нет.
- Личные пути были в 5 документах; заменены на относительные/нейтральные:
  `README.md`, `PROGRESS.md` (2 места), архивный `journal.md` (4),
  `proposal.md` (2), `tasks.md` (1), `publish-to-github/design.md` (1).
  Бэкапы — в `out/audit_backup/repo/` (в репозиторий не входят).
- Повторный скан: **0** личных путей.
- `graphify-out/` (граф) исключён как генерируемый; в README добавлены команды
  `graphify update . --no-cluster` и `graphify cluster-only . --no-label`.

### Ложные срабатывания
- `.gitignore`: слово `secrets` в комментарии.
- `publish-to-github/design.md`: перечень искомых слов в описании проверки.

## 2026-11-26 — G3 (документы)

- README: добавлены «Как скачать данные и собрать», «Зависимости»,
  «Ограничения», «Проверка (`check.bat`)», «Лицензия», «Ссылки и благодарности».
- Источник данных: FlyWire/Princeton public release v783. Официальная страница
  [flywire.ai/guidelines](https://flywire.ai/guidelines) подтверждает
  **CC BY-NC 4.0** (некоммерческое использование, атрибуция).
- Цитирование: Dorkenwald et al., Nature 634, 124–138 (2024),
  doi:10.1038/s41586-024-07558-y; Schlegel et al., Nature 634, 139–152 (2024),
  doi:10.1038/s41586-024-07686-5.
- Зависимости: MinGW-W64 g++ 16.2.0 (UCRT/POSIX/SEH), ffmpeg N-127957,
  yt-dlp 2026.10.21; Windows-only (Winsock, winmm).
- Код-лицензия: **MIT** (выбор владельца), `LICENSE` создан.
  Copyright (c) 2026 EgoriyPrivatBank. Данные — отдельно, CC BY-NC 4.0.
- CONTRIBUTING/CI предложены, но не созданы (нет запроса).

## 2026-11-26 — G4 (локальный Git, подготовка)

- `git add .`: **42 файла**, **5222** вставки, объекты **136.88 KiB**.
- Запрещённых файлов в индексе нет (exe/csv/bin/log/mp4/out/tools/backup — 0).
- `git diff --cached --check`: чисто (исправлен trailing whitespace в README).
- Git identity (local): `EgoriyPrivatBank`,
  `139873688+EgoriyPrivatBank@users.noreply.github.com`.
- Сделаны коммиты:
  - `a65f088` Add repository metadata;
  - `63ea8fc` Add flysim source, build and regression scripts;
  - третий — документация, OpenSpec-журнал и MIT `LICENSE`.
- `gh` установлен через winget: **2.106.0** (`C:\Program Files\GitHub CLI\gh.exe`).
- `gh auth status`: **не авторизован**. Нужен `gh auth login` владельца.
- `G5.1`: логин есть; имя репозитория, описание и видимость ждут решения.
  Предложение: `fly-connectome-sim`, private, описание
  "Whole-brain Drosophila connectome simulation watching video".

### Итог G4
- Коммиты: `a65f088`, `63ea8fc`, `b78730d`; автор
  `EgoriyPrivatBank <139873688+EgoriyPrivatBank@users.noreply.github.com>`.
- Отслеживается **43 файла**, объекты **161.4 KiB**, `git status` чистый.
- Remote пока нет. Тегов нет.

### G5 — блокер
- `gh` 2.106.0 установлен в `C:\Program Files\GitHub CLI\gh.exe`.
- `gh auth status` → **не авторизован**. Нужен интерактивный
  `gh auth login` владельца (браузер/HTTPS); токены в файлы/чат не пишем.
- После авторизации: создать **private** `fly-connectome-sim` без push,
  показать remote и план, затем push после подтверждения.

## 2026-11-26 — G5 (GitHub) и G6 (проверка)

### G5
- `gh auth status`: вошёл `EgoriyPrivatBank`, scopes `gist`, `read:org`, `repo`,
  `workflow`; токен не сохранялся в файлы/чат.
- `gh auth setup-git` выполнен.
- Создан **private** репозиторий:
  [EgoriyPrivatBank/fly-connectome-sim](https://github.com/EgoriyPrivatBank/fly-connectome-sim).
- Описание: `Whole-brain Drosophila connectome simulation watching video`.
- Topics: `drosophila`, `connectome`, `computational-neuroscience`, `cpp`,
  `flywire`, `simulation`.
- Push: `main` → `origin/main`, 4 коммита, 43 файла, ~167 KiB; ветка `main`.

### G6
- `gh repo view`: visibility **PRIVATE**, default branch **main**, URL верный.
- `git ls-remote`: `refs/heads/main` = `dd41932…`.
- Клон в `%TEMP%\flysim_clone_test`: 43 файла; `src/`, README, LICENSE, build.bat,
  check.bat на месте; exe/csv/bin/log/out/tools/backup — **0**.
- `build.bat` в клоне без инструментов: корректная ошибка
  `ERROR: g++ not found ... or PATH`, exit 1; полная сборка не запускалась.
- Временный клон удалён.

### Public
Репозиторий остаётся **PRIVATE**. Public не переключался.
