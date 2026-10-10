# Сборка RE4Craft

Windows, Python 3.10+, Git, Visual Studio 2022 с C++ v143 и Windows SDK 10.0.26100.0, Java 25. Для установщика используется компилятор C# из .NET Framework 4.x, присутствующий в Windows; интерфейс рассчитан на .NET Framework 4.8.

Скрипты сборки не устанавливают мод и не запускают игры. Не отправляйте папки `tools`, `build`, `private-cache` или собственные игровые файлы в Git.

## Исходники и зависимости

```powershell
.\scripts\Fetch-Sources.ps1
```

Скрипт получает закреплённые версии:

- re4_tweaks: `92c0208bd09c29c9640e13a2493b8e6bd6edd700`, включая submodules.
- PEAKCraft: `d07030ee527ee2a2facc989f5acae8a317968c02`.
- Font Awesome Free 6.7.2 и Fabric API 0.161.0+26.3 с проверкой SHA-256.

Если папка источников уже существует с другой ревизией, скрипт остановится; он не сбрасывает локальные изменения. `prepare_fork.py` применяет идемпотентные патчи к закреплённому re4_tweaks и отключает автоматическое обновление DLL, установочные события сборки и предложение менять EXE.

## Native DLL

```powershell
.\scripts\Build-Mod.ps1
# Результат: build/bridge/dinput8.dll
```

DLL собирается для Win32 с C++17. Публичная сборка заменяет встроенный upstream Pro-шрифт на неизменённый Font Awesome Free. Оригинальные файлы игры не участвуют в сборке native адаптера.

## Гостевой мод

Сначала соберите исходный SkyCraft JAR из закреплённого PEAKCraft с Java 25:

```powershell
$env:JAVA_HOME = 'C:\path\to\jdk-25'
Push-Location tools/PeakCraft/fabric
.\gradlew.bat build
Pop-Location
python scripts/build_guest_patch.py --original tools/PeakCraft/fabric/build/libs/skycraft-0.1.2.jar --libraries 'C:\path\to\Prism\libraries' --fabric-api build/dependencies/fabric-api-0.161.0+26.3.jar
# Результат: build/guest/skycraft-0.1.2-re4.2.jar
```

Либо передайте путь к имеющемуся SkyCraft 0.1.2 через `--original` и Java через `--java-home`. Папка `--libraries` должна содержать установленный Minecraft 26.3 и его зависимости. Скрипт заменяет классы из `src/guest`, регистрирует исправление поиска пути и обновляет метаданные; исходный JAR остаётся на месте. Minecraft-клиент и его ресурсы в результат не копируются. Gradle получает зависимости штатно.

Параметр `--test-mod` дополнительно собирает отдельный `re4craft-integration-test.jar` из `tests/GuestIntegration.java`. Только для разработки: положите его рядом с гостевым модом, запустите обычную комнату RE4 и дождитесь подключения мира. Временные сущности проверяют голема, поиск пути на native полу и направление блока щита; результат записывается в `re4craft-integration-results.txt` и строки `RE4CRAFT_TEST` журнала Minecraft. Затем закройте игры и уберите тестовый JAR. Установщик его не включает.

## Проверки

```powershell
# Из Developer PowerShell Visual Studio:
msbuild tests/projection.vcxproj /p:Configuration=Release /p:Platform=Win32
.\build\tests\projection.exe
.\scripts\Build-Installer.ps1 -TestBuild
.\build\installer-tests\RE4Craft-Setup.exe --self-test --test-root build/installer-tests
```

Проверка установщика использует новый каталог с искусственными файлами. Она проверяет обновления, первую резервную копию, частичный сбой записи, отказ на неподходящем EXE/повреждённой копии и сохранение нового/существующего мира. `-TestBuild` не добавляет запрос администратора; без CLI-параметров он открывает предпросмотр интерфейса с отключёнными действиями установки/удаления. Такой EXE не предназначен для распространения.

Дополнительная проверка совместимости использует собственный оригинальный `bio4.exe` только для чтения. Соберите обычный установщик, затем выполните:

```powershell
New-Item -ItemType Directory -Path build/compatibility-tests -Force
Copy-Item dist/RE4Craft-0.3.3/RE4Craft-Setup.exe* build/compatibility-tests/
& "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /out:build\compatibility-tests\InstallerCompatibility.exe /reference:build\compatibility-tests\RE4Craft-Setup.exe tests\InstallerCompatibility.cs
.\build\compatibility-tests\InstallerCompatibility.exe 'C:\path\to\Resident Evil 4\Bin32\bio4.exe' build/compatibility-tests
```

Тест создаёт отдельные копии оригинала и вариантов 4GB/LAA/контрольной суммы в новом каталоге `build`, проверяет установку/обновления/удаление, восстановление первого загрузчика и сохранность мира. Правки кода, других флагов заголовка, обрезанный и пустой EXE должны отклоняться до записи. Исходный игровой файл не меняется. Копии EXE остаются локально в игнорируемой папке и не включаются в пакет исходников или выпуск.

## Установщик и выпуск

```powershell
.\scripts\Build-Installer.ps1
python scripts/package_release.py
```

Итоговый EXE содержит DLL, два модовых JAR, настройки, профиль Prism, руководство и `installer/assets/theme.mp3`. Манифест требует администратора для папок Program Files. Рядом с EXE должен оставаться `RE4Craft-Setup.exe.config` для масштабирования. ZIP содержит EXE, его конфигурацию, документацию, лицензии и контрольные суммы. `package_release.py` составляет исходный пакет по явному списку и проверяет отсутствие игровых файлов, журналов и личных путей. Выпуск находится в `dist/RE4Craft-0.3.3`.

Установщик не подписан. Сборка на разных машинах может отличаться хешем из-за компилятора и метаданных PE; SHA256SUMS относится к конкретному опубликованному выпуску.
