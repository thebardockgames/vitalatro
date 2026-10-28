// Page flow: pick the game file -> the worker checks it (format, version) -> build -> download.
// All the work happens in js/worker.js; nothing is uploaded anywhere.
(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var drop = $('drop'), input = $('file');
  var fileStatus = $('file-status'), buildStatus = $('build-status');
  var buildBtn = $('build'), stepBuild = $('step-build');
  var progress = $('progress'), fill = $('progress-fill'), ptext = $('progress-text'), ppct = $('progress-pct');
  var download = $('download');

  var worker = null;
  var busy = false;
  var downloadUrl = null;

  function status(el, kind, label, text) {
    el.hidden = false;
    el.className = 'status ' + kind;
    el.textContent = '';
    if (label) {
      var b = document.createElement('span');
      b.className = 'label';
      b.textContent = label + ' ';
      el.appendChild(b);
    }
    el.appendChild(document.createTextNode(text || ''));
  }

  function setProgress(value, label) {
    var pct = Math.max(0, Math.min(100, Math.round(value * 100)));
    fill.style.width = pct + '%';
    ppct.textContent = pct + '%';
    if (label) ptext.textContent = label;
  }

  function resetBuild() {
    buildBtn.disabled = true;
    stepBuild.setAttribute('aria-disabled', 'true');
    progress.hidden = true;
    buildStatus.hidden = true;
    download.hidden = true;
    if (downloadUrl) { URL.revokeObjectURL(downloadUrl); downloadUrl = null; }
  }

  function getWorker() {
    if (worker) return worker;
    worker = new Worker('js/worker.js');
    worker.onmessage = onMessage;
    worker.onerror = function (e) {
      busy = false;
      status(buildStatus, 'err', 'Error:', 'el generador falló (' + (e.message || 'error desconocido') + ').');
      worker = null;
    };
    return worker;
  }

  function sizeMB(n) { return (n / 1048576).toFixed(1).replace('.', ',') + ' MB'; }

  function onMessage(ev) {
    var m = ev.data;
    if (m.type === 'inspect') {
      busy = false;
      if (m.ok) {
        status(fileStatus, 'ok', '✔ Balatro ' + m.version + ' (' + m.kindLabel + ').',
          'Versión compatible. Ya puedes generar el VPK.');
        buildBtn.disabled = false;
        stepBuild.setAttribute('aria-disabled', 'false');
      } else {
        status(fileStatus, 'err', '✘', m.error);
      }
    } else if (m.type === 'progress') {
      setProgress(m.value, m.label);
    } else if (m.type === 'done') {
      busy = false;
      setProgress(1, 'Listo');
      downloadUrl = URL.createObjectURL(m.blob);
      download.href = downloadUrl;
      download.hidden = false;
      status(buildStatus, 'ok', '✔ VPK generado', '(' + sizeMB(m.blob.size) + '). Descárgalo e instálalo en tu Vita.');
      buildBtn.disabled = false;
    } else if (m.type === 'error') {
      busy = false;
      status(m.stage === 'inspect' ? fileStatus : buildStatus, 'err', '✘ Error:', m.message);
      buildBtn.disabled = m.stage === 'inspect';
    }
  }

  function choose(file) {
    if (!file || busy) return;
    resetBuild();
    busy = true;
    status(fileStatus, '', '…', 'Comprobando ' + file.name + ' (' + sizeMB(file.size) + ')…');
    getWorker().postMessage({ cmd: 'inspect', file: file });
  }

  input.addEventListener('change', function () { choose(input.files[0]); input.value = ''; });
  drop.addEventListener('keydown', function (e) {
    if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); input.click(); }
  });
  ['dragenter', 'dragover'].forEach(function (t) {
    drop.addEventListener(t, function (e) { e.preventDefault(); drop.classList.add('over'); });
  });
  ['dragleave', 'drop'].forEach(function (t) {
    drop.addEventListener(t, function (e) { e.preventDefault(); drop.classList.remove('over'); });
  });
  drop.addEventListener('drop', function (e) {
    var f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
    choose(f);
  });
  // a file dropped outside the box must not navigate away from the page
  window.addEventListener('dragover', function (e) { e.preventDefault(); });
  window.addEventListener('drop', function (e) { e.preventDefault(); });

  buildBtn.addEventListener('click', function () {
    if (busy) return;
    busy = true;
    buildBtn.disabled = true;
    download.hidden = true;
    buildStatus.hidden = true;
    progress.hidden = false;
    setProgress(0, 'Preparando…');
    getWorker().postMessage({ cmd: 'build' });
  });
})();
