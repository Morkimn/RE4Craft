# Сборка RE4Craft

Windows, Python 3.10+, Git, Visual Studio 2022 с C++ v143 и Windows SDK 10.0.26100.0, Java 25. Для установщика используется компилятор C# из .NET Framework 4.x, присутствующий в Windows.

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
python scripts/build_guest_patch.py
# Результат: build/guest/skycraft-0.1.2-re4.1.jar
```

Либо передайте путь к оригинальному SkyCraft 0.1.2 через `--original` и Java через `--java-home`. Скрипт заменяет только классы SkyRay и метаданные мода; исходный JAR остаётся на месте. Minecraft-клиент и его ресурсы в результат не копируются. Gradle получает зависимости штатно.

## Проверки

```powershell
# Из Developer PowerShell Visual Studio:
msbuild tests/projection.vcxproj /p:Configuration=Release /p:Platform=Win32
.\build\tests\projection.exe
.\scripts\Build-Installer.ps1 -TestBuild
.\build\installer-tests\RE4Craft-Setup.exe --self-test --test-root build/installer-tests
```

Проверка установщика использует новый каталог с искусственными файлами. Она проверяет обновления, первую резервную копию, частичный сбой записи, отказ на неподходящем EXE/повреждённой копии и сохранение нового/существующего мира. `-TestBuild` не добавляет запрос администратора; такой EXE не предназначен для распространения.

## Установщик и выпуск

```powershell
.\scripts\Build-Installer.ps1
python scripts/package_release.py
```

Итоговый EXE содержит DLL, два модовых JAR, настройки, профиль Prism и руководство. Манифест требует администратора для папок Program Files. ZIP содержит EXE, документацию, лицензии и контрольные суммы. `package_release.py` составляет исходный пакет по явному списку и проверяет отсутствие игровых файлов, журналов и личных путей.

Установщик не подписан. Сборка на разных машинах может отличаться хешем из-за компилятора и метаданных PE; SHA256SUMS относится к конкретному опубликованному выпуску.
