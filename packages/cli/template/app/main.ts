// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// main.ts — proceso principal (sidecar Node). Estilo Electron.
//
import { app, BrowserWindow } from '@owear/core'

let windows = 0

app.whenReady().then(() => {
  const win = new BrowserWindow({
    title: 'Mi App Owear',
    width: 1100,
    height: 720,
    titleBarStyle: 'custom', // titlebar custom + drag regions CSS
    url: process.env.OW_DEV_SERVER_URL ?? 'app://index.html',
  })
  windows++

  win.on('closed', () => {
    windows--
    if (windows === 0) app.quit()
  })

  // ejemplo: evaluar JS en la página
  win.on('ready-to-show', async () => {
    console.log('[main] ventana lista, id =', win.id)
  })
})
