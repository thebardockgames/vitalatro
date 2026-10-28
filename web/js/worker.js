// Stub builder (phase 1): fakes the check and the build so the page flow can be exercised.
// Replaced by the WebAssembly asset builder.
'use strict';
var file = null;

self.onmessage = function (ev) {
  var m = ev.data;
  if (m.cmd === 'inspect') {
    file = m.file;
    var name = (file.name || '').toLowerCase();
    if (!/\.(exe|apk)$/.test(name)) {
      self.postMessage({ type: 'inspect', ok: false, error: 'Elige Balatro.exe (Steam) o el .apk de Android.' });
      return;
    }
    self.postMessage({ type: 'inspect', ok: true, version: '1.0.1o', kindLabel: name.endsWith('.apk') ? 'Android' : 'Steam' });
  } else if (m.cmd === 'build') {
    var steps = 20, i = 0;
    var timer = setInterval(function () {
      i++;
      self.postMessage({ type: 'progress', value: i / steps, label: 'Paso ' + i + ' de ' + steps });
      if (i === steps) {
        clearInterval(timer);
        self.postMessage({ type: 'done', blob: new Blob(['stub'], { type: 'application/zip' }) });
      }
    }, 100);
  }
};
