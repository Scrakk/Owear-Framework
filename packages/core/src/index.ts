// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// @owear/core — SDK JavaScript del kernel Owear.
//
// El kernel nativo expone un Control Socket (UDS / named pipe). Este SDK
// habla NDJSON con él: {"id":N,"cmd":"...","params":{...}}.
//
// Uso (main process, estilo Electron):
//   import { app, BrowserWindow } from '@owear/core'
//   app.whenReady().then(() => {
//     const win = new BrowserWindow({ width: 1200, height: 800 })
//     win.loadURL('http://localhost:5173')
//   })
//

import * as net from 'node:net'
import { EventEmitter } from 'node:events'
import * as os from 'node:os'
import * as path from 'node:path'
import * as fs from 'node:fs'

// ── tipos de la API pública ─────────────────────────────────────────────────

export interface WindowOptions {
  title?: string
  width?: number
  height?: number
  x?: number
  y?: number
  resizable?: boolean
  frameless?: boolean
  titleBarStyle?: 'default' | 'hidden' | 'custom'
  titleBarOverlay?: { color?: string; symbolColor?: string; height?: number }
  url?: string
}

export interface Bounds {
  x: number
  y: number
  width: number
  height: number
}

export type WindowEventMap = {
  resize: Bounds
  move: Bounds
  focus: void
  blur: void
  maximize: void
  unmaximize: void
  enterFullScreen: void
  leaveFullScreen: void
  closeRequested: void
  closed: void
}

// ── transporte de control ───────────────────────────────────────────────────

type PendingEntry = { resolve: (v: any) => void; reject: (e: Error) => void }

class ControlChannel extends EventEmitter {
  private socket: net.Socket | null = null
  private buffer = ''
  private nextId = 1
  private pending = new Map<number, PendingEntry>()

  connect(socketPath: string): Promise<void> {
    return new Promise((resolve, reject) => {
      const sock = net.connect(socketPath)
      sock.once('connect', () => {
        this.socket = sock
        resolve()
      })
      sock.once('error', reject)
      sock.on('data', (chunk: Buffer) => {
        this.buffer += chunk.toString('utf8')
        let idx: number
        while ((idx = this.buffer.indexOf('\n')) >= 0) {
          const line = this.buffer.slice(0, idx)
          this.buffer = this.buffer.slice(idx + 1)
          if (line.trim()) this.handleLine(line)
        }
      })
      sock.on('close', () => {
        this.socket = null
        this.emit('disconnected')
      })
    })
  }

  private handleLine(line: string) {
    let msg: any
    try {
      msg = JSON.parse(line)
    } catch {
      return
    }
    if (msg.event) {
      this.emit(msg.event, msg.params)
      return
    }
    const p = this.pending.get(msg.id)
    if (!p) return
    this.pending.delete(msg.id)
    if (msg.ok) p.resolve(msg.result)
    else p.reject(new Error(msg.error ?? 'error de control'))
  }

  call<T = any>(cmd: string, params: Record<string, unknown> = {}): Promise<T> {
    if (!this.socket) return Promise.reject(new Error('control socket desconectado'))
    const id = this.nextId++
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve, reject })
      this.socket!.write(JSON.stringify({ id, cmd, params }) + '\n')
    })
  }
}

// ── app ─────────────────────────────────────────────────────────────────────

const channel = new ControlChannel()

function socketPathFromEnv(): string {
  if (process.env.OW_CONTROL_SOCKET) return process.env.OW_CONTROL_SOCKET
  // fallback: último socket vivo en XDG_RUNTIME_DIR (útil para depurar)
  const dir = process.env.XDG_RUNTIME_DIR
  if (dir) {
    try {
      const socks = fs
        .readdirSync(dir)
        .filter((f) => f.startsWith('owear-') && f.endsWith('.sock'))
        .sort()
      if (socks.length) return path.join(dir, socks[socks.length - 1])
    } catch {
      /* noop */
    }
  }
  throw new Error(
    'Owear: OW_CONTROL_SOCKET no definida. Lanza la app con `ow dev` o `ow build && owear`.'
  )
}

let readyPromise: Promise<void> | null = null

export const app = {
  /** Conecta con el kernel. Resuelve cuando el canal de control está listo. */
  whenReady(): Promise<void> {
    if (!readyPromise) {
      readyPromise = channel.connect(socketPathFromEnv()).then(() => undefined)
    }
    return readyPromise
  },

  quit(exitCode = 0): Promise<void> {
    return channel.call('app.quit', { exitCode }).catch(() => undefined)
  },

  info(): Promise<{ pid: number; version: string; socket: string }> {
    return channel.call('app.info')
  },

  /** Garantiza un runtime Node gestionado por Owear (descarga oficial + SHA256). */
  ensureNodeRuntime(range = 'latest'): Promise<{ path: string }> {
    return channel.call('node.ensure', { range })
  },

  /** Acceso crudo al canal (para módulos custom del SDK). */
  __channel: channel,
}

// ── BrowserWindow ───────────────────────────────────────────────────────────

export class BrowserWindow extends EventEmitter {
  private _id: number | null = null
  private _options: WindowOptions

  constructor(options: WindowOptions = {}) {
    super()
    this._options = options
    if (!readyPromise) {
      throw new Error('BrowserWindow creado antes de app.whenReady()')
    }
    readyPromise.then(() => this._create()).catch((e) => this.emit('error', e))
  }

  private async _create(): Promise<void> {
    const params: Record<string, unknown> = { ...this._options }
    const res = await channel.call<{ windowId: number }>('window.create', params)
    this._id = res.windowId
    this._wireEvents()
    this.emit('ready-to-show', this._id)
  }

  private _wireEvents() {
    const handler = (params: any) => {
      if (params.windowId !== this._id) return
      this.emit(params.name, params.payload)
      if (params.name === 'closed') this.emit('closed')
    }
    channel.on('window.event', handler)
    channel.on('disconnected', () => this.emit('disconnected'))
  }

  get id(): number | null {
    return this._id
  }

  private requireId(): number {
    if (this._id == null) throw new Error('ventana aún no creada')
    return this._id
  }

  // ── carga ──────────────────────────────────────────────────────────────
  loadURL(url: string): Promise<void> {
    return channel.call('window.loadURL', { windowId: this.requireId(), url })
  }

  /** Evalúa JS; devuelve el resultado JSON-serializado. */
  eval<T = unknown>(js: string): Promise<T> {
    return channel.call<T>('window.eval', { windowId: this.requireId(), js })
  }

  // ── ciclo de vida ──────────────────────────────────────────────────────
  show(): Promise<void> {
    return channel.call('window.show', { windowId: this.requireId() })
  }
  hide(): Promise<void> {
    return channel.call('window.hide', { windowId: this.requireId() })
  }
  focus(): Promise<void> {
    return channel.call('window.focus', { windowId: this.requireId() })
  }
  close(): Promise<void> {
    return channel.call('window.close', { windowId: this.requireId() })
  }
  destroy(): Promise<void> {
    return channel.call('window.destroy', { windowId: this.requireId() })
  }
  minimize(): Promise<void> {
    return channel.call('window.minimize', { windowId: this.requireId() })
  }
  maximize(): Promise<void> {
    return channel.call('window.maximize', { windowId: this.requireId(), enabled: true })
  }
  unmaximize(): Promise<void> {
    return channel.call('window.maximize', { windowId: this.requireId(), enabled: false })
  }
  setFullScreen(enabled: boolean): Promise<void> {
    return channel.call('window.setFullScreen', { windowId: this.requireId(), enabled })
  }

  /**
   * Responde a un `closeRequested` (F3.4).
   * `allow=false` cancela el cierre; `true` destruye la ventana.
   * Si nadie responde en 300 ms, el kernel cierra igualmente.
   */
  closeRespond(requestId: number, allow: boolean): Promise<void> {
    return channel.call('window.respondCloseRequest', {
      windowId: this.requireId(),
      requestId,
      allow,
    })
  }

  // ── estado ─────────────────────────────────────────────────────────────
  isMaximized(): Promise<boolean> {
    return channel.call('window.isMaximized', { windowId: this.requireId() })
  }
  isMinimized(): Promise<boolean> {
    return channel.call('window.isMinimized', { windowId: this.requireId() })
  }
  getBounds(): Promise<Bounds> {
    return channel.call('window.getBounds', { windowId: this.requireId() })
  }
  setBounds(b: Partial<Bounds>): Promise<void> {
    return channel.call('window.setBounds', { windowId: this.requireId(), ...b })
  }
  setTitle(title: string): Promise<void> {
    return channel.call('window.setTitle', { windowId: this.requireId(), title })
  }
}

// ── utilidades del renderer (tipos del bridge inyectado) ────────────────────

/** Tipos de `window.ow` disponible dentro del WebView (inyectado por el kernel). */
export interface OwBridge {
  invoke<T = unknown>(module: string, fn: string, ...args: unknown[]): Promise<T>
  on(name: string, cb: (payload: any) => void): () => void
  emit(name: string, payload?: unknown): void
}

declare global {
  interface Window {
    ow?: OwBridge
  }
}

export const platform = os.platform()
