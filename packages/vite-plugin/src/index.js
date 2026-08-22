// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/vite-plugin — integra módulos nativos (.cpp) con el frontend.
//
// - Compila native/*.cpp a .owm al iniciar el build (vía owear-build-native).
// - Expone `virtual:@owear/native`: proxies tipados que llaman ow.invoke().
//
// Uso en vite.config.ts:
//   import owear from '@owear/vite-plugin'
//   export default { plugins: [owear()] }
//
// En el renderer:
//   import { files } from '@owear/native'
//   const text = await files.readText('/etc/hostname')
//

import { spawnSync } from 'node:child_process'
import * as fs from 'node:fs'
import * as path from 'node:path'

const VIRTUAL_ID = 'virtual:@owear/native'
const RESOLVED_ID = '\0' + VIRTUAL_ID

function scanModuleFunctions(cppPath) {
  // extrae nombres de OW_FN(nombre) del fuente
  const src = fs.readFileSync(cppPath, 'utf8')
  const names = [...src.matchAll(/OW_FN\(\s*([A-Za-z_]\w*)\s*\)/g)].map((m) => m[1])
  return [...new Set(names)]
}

function renderTypeDeclarations(modules) {
  const lines = []
  lines.push('// generado por @owear/vite-plugin — no editar')
  lines.push("declare module '@owear/native' {")
  for (const [mod, fns] of Object.entries(modules)) {
    const body = fns.map((fn) => `    ${fn}(...args: any[]): Promise<any>`).join('\n')
    lines.push(`  export const ${mod}: {\n${body}\n  }`)
  }
  const defaultBody = Object.keys(modules)
    .map((mod) => `    ${mod}: typeof ${mod}`)
    .join('\n')
  lines.push(`  const _default: {\n${defaultBody}\n  }`)
  lines.push('  export default _default')
  lines.push('}')
  lines.push('')
  lines.push("declare module 'virtual:@owear/native' {")
  lines.push("  export * from '@owear/native'")
  lines.push("  export { default } from '@owear/native'")
  lines.push('}')
  return lines.join('\n') + '\n'
}

function writeTypeDeclarations(modules, projectRoot) {
  if (!Object.keys(modules).length) return
  const srcDir = path.join(projectRoot, 'src')
  const outDir = fs.existsSync(srcDir) ? srcDir : projectRoot
  const outFile = path.join(outDir, 'owear-native.d.ts')
  try {
    fs.writeFileSync(outFile, renderTypeDeclarations(modules), 'utf8')
  } catch {
    /* noop: la generación de tipos es best-effort */
  }
}

export default function owear(options = {}) {
  const nativeDir = options.nativeDir ?? path.join(process.cwd(), 'native')

  let modules = {}

  function refreshModules() {
    modules = {}
    if (!fs.existsSync(nativeDir)) return
    for (const file of fs.readdirSync(nativeDir)) {
      if (!file.endsWith('.cpp')) continue
      const name = path.basename(file, '.cpp')
      modules[name] = scanModuleFunctions(path.join(nativeDir, file))
    }
  }

  return {
    name: 'owear',

    configResolved() {
      refreshModules()
      writeTypeDeclarations(modules, process.cwd())
      if (!Object.keys(modules).length) return

      // compila .owm antes de bundlear (dev y build)
      const r = spawnSync(
        process.platform === 'win32' ? 'owear-build-native.cmd' : 'owear-build-native',
        [],
        {
          stdio: 'inherit',
          shell: process.platform === 'win32',
          env: {
            ...process.env,
            OW_MODULES_OUT:
              options.modulesOut ??
              path.join(process.cwd(), 'dist', 'modules'),
          },
        },
      )
      if (r.status !== 0) {
        this.error('[owear] falló la compilación de native/*.cpp')
      }
    },

    resolveId(id) {
      if (id === VIRTUAL_ID || id === '@owear/native') return RESOLVED_ID
    },

    load(id) {
      if (id !== RESOLVED_ID) return
      const parts = []
      parts.push('// generado por @owear/vite-plugin — no editar')
      parts.push('const inv = (...a) => window.ow.invoke(...a)')
      for (const [mod, fns] of Object.entries(modules)) {
        const body = fns
          .map((fn) => `  ${fn}: (...args) => inv('${mod}', '${fn}', ...args),`)
          .join('\n')
        parts.push(`export const ${mod} = {\n${body}\n}`)
      }
      parts.push('export default {' + Object.keys(modules).join(',') + '}')
      return parts.join('\n\n')
    },
  }
}
