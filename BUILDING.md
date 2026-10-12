# Сборка и установка

## Установка готового плагина (Windows)

1. Откройте вкладку **Actions** репозитория, выберите последний зелёный запуск
   **Build** и скачайте артефакт `TLEET-windows-VST3`.
2. Распакуйте архив. Внутри папка `TLEET ASH.vst3`.
3. Скопируйте папку `TLEET ASH.vst3` целиком в
   `C:\Program Files\Common Files\VST3\` (нужны права администратора).
4. В Studio Pro: **Studio Pro → Options → Locations → VST Plug-ins** →
   **Reset Blacklist** и **Scan at startup**, затем перезапустите программу.
   Плагин появится в браузере эффектов в разделе **TLEET Audio**.

## Установка готового плагина (macOS)

1. Скачайте артефакт `TLEET-macOS`.
2. `TLEET ASH.vst3` скопируйте в `~/Library/Audio/Plug-Ins/VST3/`,
   `TLEET ASH.component` в `~/Library/Audio/Plug-Ins/Components/`.
3. Сборки пока не подписаны. Если macOS блокирует плагин, выполните в Терминале:
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"TLEET ASH.vst3"`
   (и то же для `.component`).

## Сборка на своём компьютере (Windows)

1. Установите бесплатные **Build Tools for Visual Studio 2022**, отметив
   «Desktop development with C++» (CMake входит в набор), и **Git**.
2. Склонируйте репозиторий, откройте **Developer PowerShell for VS 2022** от
   администратора (через меню Пуск) и выполните в
   его папке:
   `powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Install`
3. Скрипт скачает JUCE, соберёт плагины, прогонит тесты и скопирует `.vst3`
   в `C:\Program Files\Common Files\VST3`. Первая сборка занимает 10–20 минут.

## Сборка из исходников

Нужны CMake 3.22+ и компилятор C++20:

- Windows: Visual Studio 2022 (MSVC). MinGW не поддерживается JUCE 9.
- macOS: Xcode 15+.
- Linux (только для тестов): GCC 12+ или Clang 15+, пакеты JUCE
  (`libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev
  libxinerama-dev libxcursor-dev libgl1-mesa-dev libcurl4-openssl-dev`).

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE 9.0.4 скачивается автоматически. Чтобы использовать локальную копию:
`-DTLEET_JUCE_DIR=путь/к/JUCE`.

Плагины появляются в `build/plugins/<NAME>/<NAME>_artefacts/Release/`.

## Тесты

```
cd build
ctest -C Release --output-on-failure
```

Тест-раннер можно запустить напрямую и передать фильтр по имени теста:
`tleet_tests ASH`. Отчёт и артефакты (WAV, CSV, скриншоты) пишутся в
`test-output/` в текущей папке.
