@echo off
setlocal

set "MSVC_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
set "WDK_INC=C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
set "WDK_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"

set "CC=%MSVC_ROOT%\bin\Hostx64\x64\cl.exe"
set "AS=%MSVC_ROOT%\bin\Hostx64\x64\ml64.exe"
set "LD=%MSVC_ROOT%\bin\Hostx64\x64\link.exe"

set "INCS=/I"%MSVC_ROOT%\include" /I"%WDK_INC%\km" /I"%WDK_INC%\shared" /I"%WDK_INC%\ucrt""
set "CFLAGS=/nologo /kernel /W3 /WX- /O2 /GS- /Gy /Zc:wchar_t /Zc:inline /std:c17 /TC /Zi /D_AMD64_ /DAMD64"

if not exist build mkdir build

echo [*] compiling C...
"%CC%" %CFLAGS% %INCS% /c driver\entry.c   /Fobuild\entry.obj   /Fdbuild\vc.pdb
if errorlevel 1 goto fail
"%CC%" %CFLAGS% %INCS% /c driver\svm.c     /Fobuild\svm.obj     /Fdbuild\vc.pdb
if errorlevel 1 goto fail
"%CC%" %CFLAGS% %INCS% /c driver\vmexit.c  /Fobuild\vmexit.obj  /Fdbuild\vc.pdb
if errorlevel 1 goto fail

echo [*] assembling...
"%AS%" /nologo /c /Cx /Fobuild\vmloop.obj driver\vmloop.asm
if errorlevel 1 goto fail

echo [*] linking...
"%LD%" /nologo /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
    /OUT:build\hv7.sys /PDB:build\hv7.pdb ^
    /LIBPATH:"%WDK_LIB%\km\x64" ^
    /LIBPATH:"%MSVC_ROOT%\lib\x64" ^
    build\entry.obj build\svm.obj build\vmexit.obj build\vmloop.obj ^
    ntoskrnl.lib hal.lib wmilib.lib BufferOverflowK.lib
if errorlevel 1 goto fail

echo [+] build\hv7.sys ready

if "%1"=="test" goto run_tests
goto end

:run_tests
echo.
echo [*] building layout tests...
"%CC%" /nologo /W3 /TC /std:c17 /DH7_USERMODE_TEST /Idriver /I"%WDK_INC%\ucrt" /I"%MSVC_ROOT%\include" driver\test_layout.c /Febuild\test_layout.exe /Fobuild\test_layout.obj /link /LIBPATH:"%WDK_LIB%\ucrt\x64" /LIBPATH:"%WDK_LIB%\um\x64" /LIBPATH:"%MSVC_ROOT%\lib\x64"
if errorlevel 1 goto fail
echo [*] running layout tests...
build\test_layout.exe
if errorlevel 1 goto fail
echo [*] running rust tests...
cd ctl && cargo test --release 2>&1
cd ..
goto end

:fail
echo [-] build failed
exit /b 1

:end
endlocal
