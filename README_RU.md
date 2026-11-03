# Frame Preview v0.2.0 — Geode-мод (Android ARM64, GD 2.208)

## Как собрать .geode через GitHub
1. Загрузите **всё содержимое этой папки** в репозиторий (включая скрытую папку `.github/workflows/build.yml`).
2. Откройте **Actions → Build Geode mod → Run workflow** (или просто сделайте push).
3. После успешного запуска скачайте артефакт — внутри `.geode`.
4. Если сборка упала — откройте лог шага *Build with Geode SDK* и пришлите ошибки: правятся за один заход.

Локально: `geode build -p android64` (нужны Geode CLI/SDK и Android NDK).

## Что реализовано
- Запись `.fpm.json`: Idle/Armed/Recording/Interrupted/Completed, checkpoint каждые 120 тиков, атомарная запись, partial/complete.
- Импорт/воспроизведение: `.fpm.json`, `.gdr.json` (через временная замена: папка imports (Android file picker пока не работает с этим SDK)).
- Playback P1/P2, пауза/стоп/рестарт, отпускание кнопок при выходе/смерти, предупреждение о другом уровне.
- Speedhack (CCScheduler time scale) только во время записи, 0.10–3.00, восстановление на всех путях выхода.
- Анализ: интервалы, удержания, peak/avg inputs/s, гистограмма, фоновый поток с прогрессом и отменой; маркеры на таймлайне уровня.
- Тач-интерфейс: Macros / Recorder / Playback / Analysis / Settings, открывается кнопкой «Frame Preview» в меню паузы.
- Диалог о незавершённой записи при входе в уровень: «Сохранить как неполную» / «Удалить».

## Ограничения (честно)
- **Не собрано и не проверено на устройстве** — в окружении нет Geode SDK/NDK. Проверены только тесты ядра (`tests/`).
- Бинарные `.gdr` и `.gdr2` **не поддерживаются**: файл распознаётся и показывается честная ошибка.
- Нет кнопки «Продолжить» в диалоге незавершённой записи: после смерти/рестарта в Practice счётчик кадров расходится с реальным, продолжение дало бы битый макрос. Resume работает только после ручной «Pause rec».
- Запись прерывается при смерти/рестарте уровня (макрос с чекпоинтами Practice не склеивается).
- Не проверены на 2.208: сигнатуры `processCommands`, `destroyPlayer`, `levelComplete`, API `file::pick`, `CCMenuItemExt`, `TextInput`.

## Тесты ядра
```sh
g++ -std=c++20 -Wall -Wextra -Werror src/MacroRecorder.cpp src/MacroFormat.cpp src/Playback.cpp src/Analyzer.cpp tests/test_core.cpp -o /tmp/t && /tmp/t
```
