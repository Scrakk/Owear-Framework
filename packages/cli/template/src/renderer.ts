// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// renderer.ts — corre dentro del WebView. `ow` lo inyecta el kernel.
//
import { files } from '@owear/native'

const out = document.getElementById('out')!
const demo = document.getElementById('demo')!

async function main() {
  try {
    // módulo nativo PROPIO (native/files.cpp)
    const text = await files.readText('/etc/hostname')
    demo.textContent = `hostname leído desde C++ nativo: ${text.trim()}`

    // módulo stock fs
    const st = await ow.invoke('fs', 'stat', '/etc/hosts')
    out.textContent = JSON.stringify(st, null, 2)

    // eventos de ventana
    ow.on('resize', (b) => {
      document.title = `${b.width}×${b.height}`
    })
  } catch (e) {
    demo.textContent = '✗ ' + (e as Error).message
  }
}

main()
