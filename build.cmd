@echo off
setlocal
cd /d "%~dp0"

where clang-cl >nul 2>&1 || set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
where clang-cl >nul 2>&1 || (
    echo clang-cl was not found. Install LLVM or add it to PATH.
    exit /b 1
)

if not exist bin mkdir bin
if not exist obj mkdir obj

clang-cl --target=x86_64-pc-windows-msvc -fuse-ld=lld /nologo /W4 /O1 /GS- /Gy /Gw /GR- /Zl ^
    src\main.c /Foobj\main.obj /Febin\ChromeAlwaysOnTop.exe ^
    /link /NODEFAULTLIB /ENTRY:Start /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF ^
    /MERGE:.rdata=.text /MERGE:.pdata=.text kernel32.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1

rmdir /s /q obj
for %%F in (bin\ChromeAlwaysOnTop.exe) do echo Built %%F (%%~zF bytes)
