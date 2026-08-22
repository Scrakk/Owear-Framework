#!/usr/bin/env node
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// ow — CLI del framework Owear.
//   ow create <dir>   scaffoldea una app
//   ow dev            kernel nativo + vite dev server + sidecar Node
//   ow build          build de producción (vite build + módulos .owm)
//
import { spawn, spawnSync } from 'node:child_process'
import * as fs from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)
const __dirname = path.dirname(fileURLToPath(import.meta.url))

const C = {
  reset: '\x1b[0m',
  dim: '\x1b[2m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  red: '\x1b[31m',
  cyan: '\x1b[36m',
}
const log = (msg) => console.log(`${C.dim}[ow]${C.reset} ${msg}`)
const die = (msg) => {
  console.error(`${C.red}[ow] ✗${C.reset} ${msg}`)
  process.exit(1)
}

function platformPreset() {
  switch (process.platform) {
    case 'linux': return 'linux-release'
    case 'win32': return 'windows-release'
    case 'darwin': return 'macos-release'
    default: die(`plataforma no soportada: ${process.platform}`)
  }
}

/** Localiza el repo/binario del kernel. Orden: OW_KERNEL_BIN → binario local → repo hermano. */
function findKernelBin(cwd = process.cwd()) {
  if (process.env.OW_KERNEL_BIN && fs.existsSync(process.env.OW_KERNEL_BIN)) {
    return process.env.OW_KERNEL_BIN
  }
  const exe = process.platform === 'win32' ? 'owear.exe' : 'owear'
  const candidates = [
    path.join(cwd, '.owear', 'bin', exe),
    // monorepo en desarrollo
    path.resolve(__dirname, '../../../build', platformPreset(), 'src', exe),
  ]
  for (const c of candidates) if (fs.existsSync(c)) return c
  return null
}

/** Compila el kernel si hay fuentes disponibles (repo hermano o checkout). */
function ensureKernelBuilt(cwd) {
  const existing = findKernelBin(cwd)
  if (existing) return existing

  const repoRoot = path.resolve(__dirname, '../../..')
  const hasSources = fs.existsSync(path.join(repoRoot, 'CMakeLists.txt'))
  if (!hasSources) {
    die('kernel no encontrado. Instala @owear/runtime-<platform> o define OW_KERNEL_BIN.')
  }
  log('compilando kernel nativo (primera vez)…')
  const preset = platformPreset()
  for (const args of [['--preset', preset], ['--build', '--preset', preset]]) {
    const r = spawnSync('cmake', args, { cwd: repoRoot, stdio: 'inherit' })
    if (r.status !== 0) die('fallo al compilar el kernel')
  }
  return findKernelBin(cwd)
}

// ── comandos ────────────────────────────────────────────────────────────────

async function main() {
  const [, , cmd, ...rest] = process.argv

  if (!cmd || cmd === '-h' || cmd === '--help') {
    printHelp(); return
  }

  switch (cmd) {
    case 'create': return cmdCreate(rest)
    case 'dev':    return cmdDev(rest)
    case 'build':  return cmdBuild(rest)
    default:
      die(`comando desconocido: ${cmd} (usa --help)`)
  }
}

function printHelp() {
  console.log(`
${C.cyan}owear${C.reset} — framework desktop nativo

  ${C.green}ow create <dir>${C.reset}   crea una app nueva
  ${C.green}ow dev${C.reset}            desarrollo: vite + kernel + sidecar node
  ${C.green}ow build${C.reset}          build de producción

Variables útiles:
  OW_KERNEL_BIN     ruta al binario owear
`)
}

function cmdCreate(args) {
  const dir = args[0]
  if (!dir) die('uso: ow create <dir>')
  const target = path.resolve(dir)
  if (fs.existsSync(target) && fs.readdirSync(target).length) {
    die(`el directorio ya existe y no está vacío: ${target}`)
  }
  const templateDir = path.resolve(__dirname, '../template')
  if (!fs.existsSync(templateDir)) {
    die(`template no encontrado en ${templateDir} (instala @owear/cli completo)`)
  }
  fs.cpSync(templateDir, target, { recursive: true })
  log(`app creada en ${target}`)
  log('siguientes pasos:')
  console.log(`  cd ${path.basename(target)}`)
  console.log('  pnpm install')
  console.log('  pnpm dev')
}

function runProc(cmd, args, opts = {}) {
  return new Promise((resolve) => {
    const p = spawn(cmd, args, { stdio: 'inherit', ...opts })
    p.on('exit', (code) => resolve(code ?? 1))
  })
}

async function waitForServer(url, timeoutMs = 30000) {
  const start = Date.now()
  while (Date.now() - start < timeoutMs) {
    try {
      const r = await fetch(url)
      if (r.ok) return true
    } catch { /* aún no */ }
    await new Promise((r) => setTimeout(r, 250))
  }
  return false
}

async function cmdDev() {
  const cwd = process.cwd()
  if (!fs.existsSync(path.join(cwd, 'package.json'))) die('ejecuta dentro de tu app')

  const kernelBin = ensureKernelBuilt(cwd)

  log('arrancando vite…')
  const vite = spawn('npx', ['vite', '--port', '5173', '--strictPort'], {
    cwd,
    stdio: 'inherit',
    shell: process.platform === 'win32',
  })

  const up = await waitForServer('http://localhost:5173/')
  if (!up) { vite.kill(); die('vite no arrancó') }
  log('dev server listo en http://localhost:5173')

  // compila módulos nativos de la app (native/*.cpp → .owm)
  const nativeDir = path.join(cwd, 'native')
  let modulesDir = ''
  if (fs.existsSync(nativeDir)) {
    modulesDir = path.join(cwd, '.owear', 'modules')
    fs.mkdirSync(modulesDir, { recursive: true })
    const r = spawnSync('npx', ['owear-build-native'], {
      cwd,
      stdio: 'inherit',
      shell: process.platform === 'win32',
      env: { ...process.env, OW_MODULES_OUT: modulesDir },
    })
    if (r.status !== 0) die('falló la compilación de native/*.cpp')
  }

  log(`lanzando kernel: ${kernelBin}`)
  const kernel = spawn(kernelBin, [], {
    stdio: 'inherit',
    env: {
      ...process.env,
      OW_APP_NAME: JSON.parse(fs.readFileSync(path.join(cwd, 'package.json'), 'utf8')).name ?? 'Owear App',
      OW_DEV_SERVER_URL: 'http://localhost:5173/',
      OW_APP_MAIN: path.join(cwd, 'app', 'main.ts'),
      ...(modulesDir ? { OW_MODULES_DIR: modulesDir } : {}),
    },
  })
  kernel.on('exit', (code) => {
    log(`kernel terminó (${code})`)
    vite.kill('SIGTERM')
    process.exit(code ?? 0)
  })
  process.on('SIGINT', () => {
    kernel.kill('SIGTERM')
    vite.kill('SIGTERM')
    process.exit(0)
  })
}

async function cmdBuild() {
  const cwd = process.cwd()
  log('vite build…')
  const code = await runProc('npx', ['vite', 'build'], { cwd, shell: process.platform === 'win32' })
  if (code !== 0) die('vite build falló')

  const nativeDir = path.join(cwd, 'native')
  if (fs.existsSync(nativeDir)) {
    const out = path.join(cwd, 'dist', 'modules')
    const r = spawnSync('npx', ['owear-build-native'], {
      cwd,
      stdio: 'inherit',
      shell: process.platform === 'win32',
      env: { ...process.env, OW_MODULES_OUT: out },
    })
    if (r.status !== 0) die('falló la compilación de native/*.cpp')
  }
  log(`build lista en dist/ — sirve con OW_ASSETS_DIR=dist ./owear`)
}

main().catch((e) => die(e?.stack ?? String(e)))
