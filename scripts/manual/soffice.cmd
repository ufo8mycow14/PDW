@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "PDW_CONVERT="
set "PDW_OUTDIR="
set "PDW_INPUT="

:parse
if "%~1"=="" goto convert
if /I "%~1"=="--convert-to" (
  set "PDW_CONVERT=%~2"
  shift
  shift
  goto parse
)
if /I "%~1"=="--outdir" (
  set "PDW_OUTDIR=%~2"
  shift
  shift
  goto parse
)
set "PDW_INPUT=%~1"
shift
goto parse

:convert
if /I not "%PDW_CONVERT%"=="pdf" exit /b 1
if not defined PDW_OUTDIR exit /b 2
if not defined PDW_INPUT exit /b 3

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0word_to_pdf.ps1" -InputPath "%PDW_INPUT%" -OutputDirectory "%PDW_OUTDIR%"
exit /b %ERRORLEVEL%
