#!/usr/bin/env python3
"""Linux/macOS equivalent of generate_gl_compute_twins.ps1 for the kernels named on the command line.

HLSL -> glslangValidator (-D -S comp, entry CS, T850_SPIRV/T850_VULKAN/T850_GL) -> SPIR-V
-> spirv-cross --version 430 -> GLSL twin next to the HLSL, with the same banner (HLSL sha256)
and CRLF line endings as the PowerShell generator, so check_gl_twins.py accepts it.

Usage: generate_gl_compute_twins.py [--check] CS_Name.hlsl [...]
Tools come from PATH, or from the GLSLANG and SPIRV_CROSS environment variables.
"""
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SHADERS = Path(__file__).resolve().parents[2] / 'Assets' / 'Shaders'
GLSLANG = os.environ.get('GLSLANG', 'glslangValidator')
SPIRV_CROSS = os.environ.get('SPIRV_CROSS', 'spirv-cross')
DEFINES = ['T850_SPIRV=1', 'T850_VULKAN=1', 'T850_GL=1']


def generate(source: str, check_only: bool) -> bool:
    path = SHADERS / Path(source).name
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory() as tmp:
        spv = Path(tmp) / f'{path.stem}.spv'
        raw = Path(tmp) / f'{path.stem}.raw.glsl'
        cmd = [GLSLANG, '-D', '-S', 'comp', '-e', 'CS', '--target-env', 'vulkan1.0']
        cmd += [f'-D{d}' for d in DEFINES] + ['-o', str(spv), str(path)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f'FAIL {path.name} (glslangValidator)\n{result.stdout}{result.stderr}')
            return False
        if check_only:
            print(f'ok   {path.name} compiles')
            return True
        result = subprocess.run([SPIRV_CROSS, str(spv), '--version', '430', '--no-es',
                                 '--combined-samplers-inherit-bindings', '--output', str(raw)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(f'FAIL {path.name} (spirv-cross)\n{result.stdout}{result.stderr}')
            return False
        body = raw.read_text().splitlines()
    if not body or not body[0].startswith('#version'):
        print(f'FAIL {path.name}: spirv-cross output has no #version line')
        return False
    banner = (f'// GENERATED from {path.name} by scripts/shaders/generate_gl_compute_twins.ps1, '
              f'do not edit; hlsl-sha256={digest}; defines={",".join(DEFINES)}')
    lines = [body[0], banner] + body[1:]
    (SHADERS / f'{path.stem}.glsl').write_bytes(('\r\n'.join(lines) + '\r\n').encode('utf-8'))
    print(f'write {path.stem}.glsl')
    return True


if __name__ == '__main__':
    check = '--check' in sys.argv
    sources = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not sources:
        sys.exit(__doc__)
    sys.exit(0 if all([generate(s, check) for s in sources]) else 1)
