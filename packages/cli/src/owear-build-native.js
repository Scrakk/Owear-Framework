#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// owear-build-native — compila native/*.cpp de una app a .owm.
// Cero configuración: detecta el compilador del sistema y compila como librería
// dinámica exportando ow_module_descriptor.
//
import { spawnSync } from 'node:child_process'
import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'

const out = process.env.OW_MODULES_OUT ?? path.join(process.cwd(), '.owear', 'modules')
const nativeDir = path.join(process.cwd(), 'native')

if (!fs.existsSync(nativeDir)) {
  console.log('[ow-build-native] sin directorio native/ — nada que hacer')
  process.exit(0)
}
fs.mkdirSync(out, { recursive: true })

// incluye headers públicos del framework si están instalados junto al kernel
function frameworkIncludes() {
  const incs = []
  // desde packages/cli/src/ → raíz del repo (monorepo en desarrollo)
  const candidates = [
    process.env.OW_INCLUDE_DIR,
    path.resolve(import.meta.dirname ?? path.dirname(fileURLToPath(import.meta.url)),
                 '../../../include'),
  ].filter(Boolean)
  for (const c of candidates) if (fs.existsSync(c)) incs.push(c)
  return incs
}

function pickCompiler() {
  if (process.env.CXX) return process.env.CXX
  for (const c of ['c++', 'g++', 'clang++']) {
    if (spawnSync(c, ['--version'], { stdio: 'ignore' }).status === 0) return c
  }
  return null
}

function pickWindowsCompiler() {
  // cl.exe del último VS instalado (via vswhere)
  const vswhere = 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
  if (!fs.existsSync(vswhere)) {
    if (spawnSync('clang++', ['--version'], { stdio: 'ignore' }).status === 0) {
      return { cmd: 'clang++', msvcLike: false }
    }
    return null
  }
  const r = spawnSync(vswhere, ['-latest', '-property', 'installationPath'], { encoding: 'utf8' })
  const vsPath = r.stdout?.trim()
  if (!vsPath) return null
  const clCandidates = fs.readdirSync(path.join(vsPath, 'VC', 'Tools', 'MSVC'))
    .sort().reverse()
  for (const v of clCandidates) {
    const cl = path.join(vsPath, 'VC', 'Tools', 'MSVC', v, 'bin', 'Hostx64', 'x64', 'cl.exe')
    if (fs.existsSync(cl)) return { cmd: cl, msvcLike: true }
  }
  return null
}

const incs = frameworkIncludes()
let failures = 0

for (const file of fs.readdirSync(nativeDir)) {
  if (!file.endsWith('.cpp')) continue
  const src = path.join(nativeDir, file)
  const name = path.basename(file, '.cpp')
  const ext = process.platform === 'win32' ? '.dll' : process.platform === 'darwin' ? '.dylib' : '.so'
  const dest = path.join(out, name + ext)

  // rebuild solo si cambió la fuente
  if (fs.existsSync(dest) && fs.statSync(src).mtimeMs < fs.statSync(dest).mtimeMs) {
    console.log(`[ow-build-native] ${name} al día`)
    continue
  }

  const compiler = process.platform === 'win32' ? pickWindowsCompiler() : (() => {
    const cmd = pickCompiler()
    return cmd ? { cmd, msvcLike: false } : null
  })()
  if (!compiler) {
    console.error(`[ow-build-native] ✗ no hay compilador C++ para ${file}`)
    failures++
    continue
  }

  let args
  if (compiler.msvcLike) {
    args = ['/std:c++20', '/LD', '/O2', '/EHsc', `/Fe:${dest}`, src,
            ...incs.flatMap((i) => [`/I${i}`])]
  } else {
    args = ['-std=c++20', '-O2', '-fPIC', '-shared', '-o', dest, src,
            ...incs.flatMap((i) => ['-I', i]),
            ...(process.platform === 'darwin' ? [] : [])]
  }
  console.log(`[ow-build-native] compilando ${file} → ${path.basename(dest)}`)
  const r = spawnSync(compiler.cmd, args, { stdio: 'inherit' })
  if (r.status !== 0) {
    console.error(`[ow-build-native] ✗ falló ${file}`)
    failures++
  }
}

process.exit(failures ? 1 : 0)
