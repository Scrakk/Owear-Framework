// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Bridge/BridgeScript.cpp — script inyectado en cada documento.
// Un solo archivo para las 3 plataformas: el transporte se autodetecta
// (chrome.webview en WebView2, window.webkit.messageHandlers en WK/WebKitGTK).
#include <string>

namespace ow {

std::string BuildBridgeScript() {
    return R"JS((function() {
  if (window.__ow) return;
  var pending = new Map();
  var nextId = 1;
  var listeners = new Map();

  function send(obj) {
    try {
      if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
        window.chrome.webview.postMessage(JSON.stringify(obj));
      } else if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.ow) {
        window.webkit.messageHandlers.ow.postMessage(JSON.stringify(obj));
      } else {
        console.error('[ow] transport no disponible');
      }
    } catch (e) { console.error('[ow] send error', e); }
  }

  window.__ow = {
    _apply: function(id, ok, jsonLiteral) {
      var p = pending.get(id);
      if (!p) return;
      pending.delete(id);
      var val = null;
      try { val = (jsonLiteral == null) ? null : JSON.parse(jsonLiteral); }
      catch (e) { val = null; }
      clearTimeout(p.timer);
      if (ok) p.resolve(val);
      else {
        var msg = (val && val.message) ? val.message : String(val);
        p.reject(new Error(msg));
      }
    },
    _event: function(w, name, payloadLiteral) {
      if (w && window.__owWindowId && w !== window.__owWindowId && w !== 0) return;
      var payload = null;
      try { payload = (payloadLiteral == null) ? null : JSON.parse(payloadLiteral); }
      catch (e) {}
      var set = listeners.get(name);
      if (set) set.forEach(function(cb) {
        try { cb(payload); } catch (e) { console.error('[ow] listener', e); }
      });
    },
    _batch: function(ops) {
      for (var i = 0; i < ops.length; i++) {
        var op = ops[i];
        if (op[0] === 'a') this._apply(op[1], op[2], op[3]);
        else this._event(op[1], op[2], op[3]);
      }
    }
  };

  // Regiones de arrastre nativas: [data-ow-drag] mueve, [data-ow-resize="left|right|top|bottom|top-left|..."]
  document.addEventListener('mousedown', function(e) {
    if (e.button !== 0) return;
    var t = e.target;
    while (t && t !== document.body) {
      if (t.hasAttribute && t.hasAttribute('data-ow-resize')) {
        ow.invoke('ow-window', 'beginResizeDrag', t.getAttribute('data-ow-resize'));
        return;
      }
      if (t.hasAttribute && t.hasAttribute('data-ow-drag')) {
        ow.invoke('ow-window', 'beginMoveDrag');
        return;
      }
      t = t.parentElement;
    }
  }, true);

  window.ow = {
    invoke: function(module, fn) {
      var args = Array.prototype.slice.call(arguments, 2);
      var id = nextId++;
      return new Promise(function(resolve, reject) {
        pending.set(id, { resolve: resolve, reject: reject, timer: 0 });
        pending.get(id).timer = setTimeout(function() {
          if (pending.delete(id)) reject(new Error('ow: timeout ' + module + '/' + fn));
        }, 30000);
        send({ t: 'invoke', id: id, m: module, f: fn, a: args, w: window.__owWindowId || 0 });
      });
    },
    /* F3.3 — invoke SÍNCRONO vía ow-sync:// (XHR bloqueante).
       ⚠️ Solo para arranque/bootstrap: bloquea el renderer y el handler
       corre en el main thread del kernel (no llamar APIs que hagan eval). */
    invokeSync: function(module, fn) {
      var args = Array.prototype.slice.call(arguments, 2);
      var payload = encodeURIComponent(JSON.stringify({
        t: 'invoke', id: -1, m: module, f: fn, a: args,
        w: window.__owWindowId || 0
      }));
      var x = new XMLHttpRequest();
      x.open('GET', 'ow-sync://i/' + payload, false); // síncrono
      x.send(null);
      if (x.status !== 200) throw new Error('ow.sync: HTTP ' + x.status);
      var res = JSON.parse(x.responseText);
      if (!res.ok) throw new Error((res.r && res.r.message) || 'ow.sync error');
      return res.r;
    },
    /* F3.1 — lee una región de memoria compartida como ArrayBuffer.
       Sin base64 ni JSON: el scheme sirve el mmap directo. */
    readShared: function(handle) {
      if (!handle || !handle.id)
        return Promise.reject(new Error('ow.readShared: handle inválido'));
      return fetch('ow-shm://' + handle.id).then(function(r) {
        if (!r.ok) throw new Error('ow.readShared: HTTP ' + r.status);
        return r.arrayBuffer();
      });
    },
    /* findInPage vía window.find (fallback JS; el nativo usa FindController) */
    findInPage: function(text, opts) {
      var cs = opts && opts.matchCase ? false : true; // insensitive por defecto
      var bw = opts && opts.backwards ? true : false;
      if (!window.find || !window.find(text, cs, bw)) return { matches: 0, active: 0 };
      var n = 1;
      while (n < 1000 && window.find(text, cs, bw)) n++;
      return { matches: n, active: n };
    },
    /* IPC dirigido ventana→ventana (F-next) */
    emitTo: function(targetWindowId, name, payload) {
      if (targetWindowId === window.__owWindowId) {
        // mismo destino: despacho local inmediato
        var set0 = listeners.get(name);
        if (set0) set0.forEach(function(cb) { try { cb(payload); } catch (e) {} });
      }
      send({ t: 'event', to: targetWindowId, n: name,
             p: payload === undefined ? null : payload,
             w: window.__owWindowId || 0 });
    },
    on: function(name, cb) {
      if (!listeners.has(name)) listeners.set(name, new Set());
      listeners.get(name).add(cb);
      return function() { listeners.get(name).delete(cb); };
    },
    emit: function(name, payload) {
      // evento JS→nativo (otros listeners JS lo reciben también)
      var set = listeners.get(name);
      if (set) set.forEach(function(cb) { try { cb(payload); } catch (e) {} });
      send({ t: 'event', n: name, p: payload === undefined ? null : payload, w: window.__owWindowId || 0 });
    }
  };
})();)JS";
}

} // namespace ow
