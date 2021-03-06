del /Q /S Xenowar\*.*
del /Q Xenowar\res\*.*
del /Q /S XenowarInstaller\*.*

mkdir Xenowar
mkdir Xenowar\res
mkdir Xenowar\modtest

copy .\res\*.* Xenowar\res\*.*
copy .\modtest\*.* Xenowar\modtest

copy c:\bin\SDL.dll Xenowar
copy c:\bin\SDL_image.dll Xenowar

copy .\win32\Release\UFOAttack.exe Xenowar\Xenowar.exe
copy .\ufobuilder\win32\Release\ufobuilder.exe Xenowar\ufobuilder.exe
copy README.txt Xenowar
copy .\makemod.bat Xenowar

xcopy /S Xenowar XenowarInstaller
copy vcredist_x86.exe XenowarInstaller


