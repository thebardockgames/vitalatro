// Builds the VPK in a worker thread with the asset builder compiled to WebAssembly
// (tools/asset_builder): the user's file never leaves this browser.
//   main -> worker: {cmd: 'inspect', file} | {cmd: 'build'}
//   worker -> main: {type: 'inspect', ok, version, kindLabel, error}
//                   {type: 'progress', value, label} | {type: 'done', blob} | {type: 'error', stage, message}
'use strict';
importScripts('../builder/vl_asset_builder.js');

var modulePromise = null;
var basePromise = null;
var ready = false;  // a checked game is loaded in the module

function getModule() {
  if (!modulePromise) {
    modulePromise = VLAssetBuilder({
      locateFile: function (p) { return '../builder/' + p; },
      print: function () {},
      printErr: function (t) { console.warn(t); }
    });
  }
  return modulePromise;
}

// our prebuilt files (eboot.bin, param.sfo, LiveArea template, shaders), fetched once
function getBase() {
  if (!basePromise) {
    basePromise = fetch('../base/manifest.json').then(function (r) {
      if (!r.ok) throw new Error('no se pudo descargar base/manifest.json');
      return r.json();
    }).then(function (manifest) {
      return Promise.all(manifest.files.map(function (f) {
        return fetch('../base/' + f.path).then(function (r) {
          if (!r.ok) throw new Error('no se pudo descargar ' + f.path);
          return r.arrayBuffer();
        }).then(function (buf) {
          if (buf.byteLength !== f.size) throw new Error(f.path + ' incompleto');
          return { path: f.path, data: new Uint8Array(buf) };
        });
      }));
    });
    basePromise.catch(function () { basePromise = null; });
  }
  return basePromise;
}

function copyIn(M, bytes) {
  var ptr = M._malloc(bytes.length);
  if (!ptr) throw new Error('memoria insuficiente');
  M.HEAPU8.set(bytes, ptr);
  return ptr;
}

function info(M) { return JSON.parse(M.UTF8ToString(M._vlab_info())); }

function kindLabel(kind) {
  return kind === 'apk' ? 'Android' : kind === 'exe' ? 'Steam' : kind;
}

function inspect(file) {
  ready = false;
  return Promise.all([getModule(), file.arrayBuffer()]).then(function (r) {
    var M = r[0];
    var bytes = new Uint8Array(r[1]);
    var ptr = copyIn(M, bytes);  // vlab_open frees it
    bytes = null;
    var name = M.stringToNewUTF8(file.name || '');
    var rc = M._vlab_open(ptr, r[1].byteLength, name);
    M._free(name);
    var i = info(M);
    ready = rc === 0;
    self.postMessage({ type: 'inspect', ok: ready, version: i.version, kindLabel: kindLabel(i.kind), error: i.error });
    if (ready) getBase();  // start downloading while the user reads
  });
}

function build() {
  if (!ready) throw new Error('elige primero tu copia de Balatro');
  var progress = function (value, label) { self.postMessage({ type: 'progress', value: value, label: label }); };
  return getModule().then(function (M) {
    progress(0, 'Preparando');
    M.onProgress = function (f, label) { progress(0.02 + 0.88 * f, label); };
    if (M._vlab_build() !== 0) throw new Error(info(M).error || 'error al generar los recursos');
    ready = false;
    progress(0.9, 'Descargando la base de Vitalatro');
    return getBase().then(function (base) {
      base.forEach(function (f) {
        var p = M.stringToNewUTF8(f.path);
        M._vlab_base_add(p, copyIn(M, f.data), f.data.length);
        M._free(p);
      });
      progress(0.93, 'Empaquetando el VPK');
      if (M._vlab_make_vpk() !== 0) throw new Error(info(M).error || 'error al crear el VPK');
      var ptr = M._vlab_vpk_data(), size = M._vlab_vpk_size();
      var blob = new Blob([M.HEAPU8.slice(ptr, ptr + size)], { type: 'application/octet-stream' });
      M._vlab_close();
      self.postMessage({ type: 'done', blob: blob });
    });
  });
}

self.onmessage = function (ev) {
  var m = ev.data;
  var stage = m.cmd;
  var p;
  try {
    p = m.cmd === 'inspect' ? inspect(m.file) : m.cmd === 'build' ? build() : Promise.resolve();
  } catch (e) {
    p = Promise.reject(e);
  }
  p.catch(function (e) {
    self.postMessage({ type: 'error', stage: stage, message: (e && e.message) || String(e) });
  });
};
