@echo off
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "PROJECT=%ROOT%T850"
set "EXE_DIR=%PROJECT%\bin\x64\Debug"
set "EXE=%EXE_DIR%\DayScene.exe"
set "MODELS=%PROJECT%\Assets\Models"
set "OUT=%PROJECT%\Assets\Shaders\shader_permutations.json"
set "API=d3d11"

if not "%~1"=="" set "OUT=%~1"

if not exist "%EXE%" (
    echo [ERROR] DayScene.exe was not found at "%EXE%".
    echo [ERROR] Build T850\T850.sln Debug x64 DayScene first.
    exit /b 1
)

if not exist "%MODELS%" (
    echo [ERROR] Models directory was not found at "%MODELS%".
    exit /b 1
)

if exist "%OUT%" del "%OUT%"

pushd "%EXE_DIR%" || exit /b 1

echo [T850] Dumping DayScene shader permutations...
"%EXE%" --api %API% --scene 1 --dumpShaderPermutations --shaderPermutationOutput "%OUT%"
if errorlevel 1 (
    popd
    exit /b 1
)

echo [T850] Dumping Sandbox shader permutations for all models...
for /r "%MODELS%" %%M in (*.glb *.gltf *.x) do (
    set "MODEL=%%~fM"
    set "MODEL=!MODEL:%PROJECT%\Assets\=!"
    echo [T850]   !MODEL!
    "%EXE%" --api %API% --scene 0 --model "!MODEL!" --dumpShaderPermutations --shaderPermutationOutput "%OUT%"
    if errorlevel 1 (
        popd
        exit /b 1
    )
)

popd

echo [T850] Shader permutation dump written to "%OUT%".
exit /b 0
