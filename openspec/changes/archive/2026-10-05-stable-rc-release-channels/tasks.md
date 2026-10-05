## 1. CI: каналы, guard, релиз

- [x] 1.1 В `.github/workflows/tests.yml` добавить `fetch-depth: 0` в checkout job'а `firmware-build` (нужен граф коммитов для `merge-base --is-ancestor`).
- [x] 1.2 Вычислять канал тега: `^v[0-9]+\.[0-9]+\.[0-9]+$` → stable, `^v[0-9]+\.[0-9]+\.[0-9]+-rc[0-9]+$` → test; тег иного формата — фейл job'а.
- [x] 1.3 Guard для stable: `git fetch origin master` + `git merge-base --is-ancestor "$GITHUB_SHA" origin/master`; при неудаче — фейл и никакого релиза. Для test-guard не применять.
- [x] 1.4 Флаги релиза: stable → `--latest`, test → `--prerelease`; notes — `--notes-from-tag` при непустой аннотации (`git for-each-ref "refs/tags/$TAG" --format='%(contents)'`), иначе `--generate-notes`.
- [x] 1.5 Сохранить идемпотентность: если релиз уже существует — только `gh release upload … --clobber`, notes не трогать.
- [x] 1.6 Загружать артефакт `firmware-binaries` (`esp-ot-gateway.bin`, `bootloader.bin`, `partition-table.bin`) из job'а `firmware-build`.

## 2. CI: Pages на тегах, versions.json, sha256

- [x] 2.1 В `deploy-pages` расширить условие до `github.ref == 'refs/heads/master' || startsWith(github.ref, 'refs/tags/v')` и добавить `firmware-build` в `needs`.
- [x] 2.2 Скачивать артефакт `firmware-binaries`; на тег-ране наполнять `site/firmware/<tag>/` из артефакта и генерировать `manifest.json` из него (без `gh release download` для текущего тега).
- [x] 2.3 Считать `sha256` от того самого `esp-ot-gateway.bin`, который кладётся на Pages: для текущего тега — от артефакта, для остальных — от скачанного `.bin`.
- [x] 2.4 Формировать `versions.json`: у каждой записи `tag,name,published_at,prerelease,sha256` (поле `tag` раньше `sha256`); порядок — stable (semver desc), затем prerelease (semver desc); сортировка не зависит от даты.
- [x] 2.5 Не включать `sha256`, если для релиза нет `.bin`; проверить, что каталог остаётся < 64 КиБ.

## 3. Промоушен rc → stable

- [x] 3.1 Добавить `.github/workflows/promote.yml`: триггер `pull_request: types: [closed]`, условие `github.event.pull_request.merged == true && github.event.pull_request.base.ref == 'master'`; permissions `contents: write`, `actions: write`.
- [x] 3.2 Явный checkout базовой ветки (`ref: master`, `fetch-depth: 0`, `fetch-tags: true`) — на closed-событии дефолтный checkout может указывать на merge-ref.
- [x] 3.3 Найти rc: `git tag --merged "$HEAD_SHA" -l 'v*-rc*'` (HEAD_SHA = `pull_request.head.sha`), отфильтровать строгим регэкспом, взять старший `sort -V | tail -1`; если пусто — job завершается успешно без тега.
- [x] 3.4 Вычислить stable-имя `${RC%-rc[0-9]*}`; если тег `vX.Y.Z` уже существует — пропустить, ничего не менять (идемпотентность).
- [x] 3.5 Задать `git config user.name/email`, создать аннотированный stable-тег на `pull_request.merge_commit_sha` с аннотацией rc (`git for-each-ref "refs/tags/$RC" --format='%(contents)'` через `git tag -a -F`); `git push origin "$STABLE"`.
- [x] 3.6 Запустить релиз без секретов: `gh workflow run tests.yml --ref "$STABLE" -f source_rc="$RC"`.
- [x] 3.7 В `tests.yml` добавить input `source_rc`; после успешного создания stable-релиза (ветка «Creating new release») удалить rc-релиз и тег: `gh release delete "$RC" --yes` + `git push origin ":refs/tags/$RC"`. При «релиз уже существует» — не удалять.

## 4. Устройство: каталог и тестовая группа

- [x] 4.1 Добавить в `test/test_ota_interactor.cpp` кейс с каталогом из stable + `-rcN`, проверяющий правильную ассоциацию `tag` → `sha256` (несколько версий подряд).
- [x] 4.2 В `main/infrastructure/driving/web_page.h` группировать версии с `prerelease:true` в отдельный `<optgroup label="Тестовые">` под стабильными; тестовая версия не выбирается автоматически.
- [x] 4.3 В `otaStart()` требовать `confirm()` при выбранном тестовом теге (`-rcN`); показывать предупреждение «на свой страх и риск».
- [x] 4.4 Проверить обратную совместимость: элементы-строки и объекты без `prerelease` рендерятся как раньше (без падения).

## 5. Веб-установщик

- [x] 5.1 В `web-flasher/app.js` добавить semver-компаратор и сортировку списка по убыванию; stable перед test.
- [x] 5.2 Дефолт — высшая стабильная по semver (не первый элемент API и не по дате); тестовая никогда не выбирается автоматически.
- [x] 5.3 Вынести тестовые версии в отдельную секцию (`<optgroup label="Тестовые (на свой риск)">`) в `flasher.html`/`app.js`.
- [x] 5.4 Поддержать deep-link `flasher.html?tag=<tag>` через `URLSearchParams`: преселект (включая тестовый) с отключением авто-дефолта; неизвестный тег → дефолт.
- [x] 5.5 Перед активацией прошивки проверять `HEAD firmware/<tag>/manifest.json`: при 404 — сообщение; для авто-выбора — откат к следующей стабильной с манифестом, для явного выбора — ошибка без включения прошивки.

## 6. Документация

- [x] 6.1 Обновить `docs/ota-testing.md`: тестовый тег — `vX.Y.Z-rcN`; публикация на Pages штатным тег-раном (убрать `gh workflow run tests.yml --ref master`); в curl-проверки добавить `sha256`/порядок каталога.
- [x] 6.2 Зафиксировать регламент релиза: `merge в master → авто-промоушен rc → stable → Release + Pages`; ручной stable-путь (тег на master) как fallback; уборка rc; тесты только `-rcN`. Место — `docs/` или `CONTRIBUTING`.

## 7. Проверка

- [x] 7.1 `openspec validate stable-rc-release-channels` — без ошибок.
- [x] 7.2 Собрать и прогнать host-тесты (`cmake -B build -S test && cmake --build build -j && ./build/run_tests`) — без регрессий.
- [x] 7.3 Локально проверить shell-логику классификации тегов, guard, сортировки и подсчёта `sha256` на фейковом наборе тегов (включая `vX.Y.Z` и `vX.Y.Z-rcN`).
- [x] 7.4 Локально проверить логику промоушена: поиск старшего rc (несколько rc, разные базовые версии), снятие `-rcN`, пропуск при существующем stable.
- [x] 7.5 После влития в `master`: убедиться, что Pages пересобрал `versions.json` с `sha256` и stable-first порядком; проверить deep-link и fallback манифеста на Pages.
- [x] 7.6 Проверить тег-ран на `-rcN`: `firmware/<tag>/` и `versions.json` обновились без пуша в `master`.
- [x] 7.7 E2E-промоушен на реальном PR: merge PR с `vX.Y.Z-rcN` → stable-тег на merge-коммите, запуск `workflow_dispatch`, релиз создан, rc-релиз/тег удалены.

## 8. Безопасность обкатки (дополнения после ревью)

- [x] 8.1 `deploy-pages`: корень сайта (`web-flasher`) на тег-ранах берётся из `master` (`checkout ref: master`); из тега — только `firmware/<tag>/`. Тестовый rc-тег с feature-ветки больше не может опубликовать код ветки в прод-фронтенд.
- [x] 8.2 `promote.yml`: `::warning::`, если промоутируемая версия не выше максимальной стабильной (не блокирует легитимные патч-релизы).
