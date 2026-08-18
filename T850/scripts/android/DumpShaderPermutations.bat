@echo off
setlocal enabledelayedexpansion

for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
set "PROJECT=%ROOT%"
set "EXE_DIR=%PROJECT%\bin\x64\Debug"
set "EXE=%EXE_DIR%\DayScene.exe"
set "MODELS=%PROJECT%\Assets\Models"
set "OUT=%PROJECT%\Assets\Shaders\shader_permutations.json"
set "API=d3d11"
set /a MODEL_COUNT=0
set /a FAILED_COUNT=0

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

for /r "%MODELS%" %%M in (*.glb *.gltf *.x) do (
    set /a MODEL_COUNT+=1
)

echo [T850] Found %MODEL_COUNT% model files under "%MODELS%".

pushd "%EXE_DIR%" || exit /b 1

echo [T850] Dumping DayScene shader permutations...
"%EXE%" --api %API% --scene 1 --dumpShaderPermutations --shaderPermutationOutput "%OUT%"
if errorlevel 1 (
    echo [ERROR] DayScene shader permutation dump failed.
    popd
    exit /b 1
)

echo [T850] Dumping Sandbox shader permutations for all models...
for /r "%MODELS%" %%M in (*.glb *.gltf *.x) do call :DumpModel "%%~fM"

popd

if !FAILED_COUNT! GTR 0 (
    echo [ERROR] Shader permutation dump completed with !FAILED_COUNT! failed models.
    echo [ERROR] Partial shader permutation dump written to "%OUT%".
    exit /b 1
)

echo [T850] Shader permutation dump written to "%OUT%".
exit /b 0

:DumpModel
set "MODEL_ABS=%~1"
set "MODEL=%MODEL_ABS%"
call set "MODEL=%%MODEL:%PROJECT%\Assets\=%%"
echo [T850]   %MODEL%
"%EXE%" --api %API% --scene 0 --model "%MODEL%" --dumpShaderPermutations --shaderPermutationOutput "%OUT%"
if errorlevel 1 (
    echo [ERROR] Shader permutation dump failed for "%MODEL%".
    set /a FAILED_COUNT+=1
)
exit /b 0
