@echo off

if "%1"=="run" or "%1"=="runview" (
    set run=%1
    set exfile=%2
) else (
    set run=%2
    set exfile=%1
)

if "%exfile%"=="" (
    echo Error: No example file was given
    goto :eof
)

gcc -Wall %exfile% ../src/*.c -I../src

if %errorlevel%==0 (
    if not "%run%"=="" (
        .\a.exe

        if "%run%"=="runview" (
            start .\output_image.png
        )
    )
)
