// Animated page background: a slow red/blue paint swirl, rendered at low resolution and scaled
// up with nearest-neighbour filtering (the pixelated look). Our own shader: polar twist + a few
// rounds of sine domain warping, posterized into flat paint bands.
// Static single frame with prefers-reduced-motion; CSS gradient fallback without WebGL.
(function () {
  'use strict';
  var canvas = document.getElementById('bg');
  var PIXEL = 4;          // CSS pixels per background pixel
  var FPS = 30;

  var VS = 'attribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }';
  var FS = [
    'precision mediump float;',
    'uniform vec2 uRes;',
    'uniform float uTime;',
    'const vec3 RED = vec3(0.996, 0.373, 0.333);',
    'const vec3 BLUE = vec3(0.0, 0.612, 0.992);',
    'const vec3 DEEP = vec3(0.10, 0.12, 0.17);',
    'void main() {',
    '  vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / min(uRes.x, uRes.y);',
    '  float t = uTime * 0.03;',
    '  float r = length(uv);',
    // polar twist: tighter near the centre, slowly turning
    '  float a = atan(uv.y, uv.x) + 2.6 * exp(-1.3 * r) - t * 1.3;',
    '  vec2 q = vec2(cos(a), sin(a)) * r * 3.2;',
    // domain warp: each round folds the field a bit more (the paint curls)
    '  for (int i = 0; i < 6; i++) {',
    '    float fi = float(i);',
    '    q += 0.38 * vec2(sin(q.y * 1.9 + t * 2.1 + fi * 1.7),',
    '                     cos(q.x * 1.6 - t * 1.7 + fi * 0.9));',
    '  }',
    '  float f = 0.5 + 0.5 * sin(q.x * 1.3 + q.y * 0.7 + t * 0.7);',
    '  float g = 0.5 + 0.5 * sin(q.x * 3.1 - q.y * 2.3 - t * 1.9);',
    '  vec3 base = mix(BLUE, RED, smoothstep(0.42, 0.58, f));',
    // dark seams where the two paints meet, darker and lighter streaks inside each band
    '  float seam = smoothstep(0.0, 0.16, abs(f - 0.5));',
    '  vec3 col = mix(DEEP, base, 0.35 + 0.65 * seam);',
    '  col = mix(col, col * 0.72, smoothstep(0.55, 0.75, g) * seam);',
    '  col += vec3(0.13) * smoothstep(0.9, 1.0, g) * seam;',
    // flat paint tones
    '  col = floor(col * 6.0 + 0.5) / 6.0;',
    '  col *= 1.0 - 0.25 * r * r;',
    '  gl_FragColor = vec4(col, 1.0);',
    '}'
  ].join('\n');

  function fail() { document.body.classList.add('no-webgl'); }

  var gl = null;
  try {
    gl = canvas.getContext('webgl', { antialias: false, alpha: false, preserveDrawingBuffer: true }) ||
         canvas.getContext('experimental-webgl');
  } catch (e) { gl = null; }
  if (!gl) { fail(); return; }

  function shader(type, src) {
    var s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
    return s;
  }
  var prog;
  try {
    prog = gl.createProgram();
    gl.attachShader(prog, shader(gl.VERTEX_SHADER, VS));
    gl.attachShader(prog, shader(gl.FRAGMENT_SHADER, FS));
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
  } catch (e) { fail(); return; }

  gl.useProgram(prog);
  var buf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
  var loc = gl.getAttribLocation(prog, 'p');
  gl.enableVertexAttribArray(loc);
  gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
  var uRes = gl.getUniformLocation(prog, 'uRes');
  var uTime = gl.getUniformLocation(prog, 'uTime');

  var reduce = window.matchMedia ? window.matchMedia('(prefers-reduced-motion: reduce)') : null;
  var start = performance.now();
  var last = 0;
  var still = 40;  // the frame shown when motion is reduced (seconds)

  function resize() {
    var w = Math.max(1, Math.ceil(window.innerWidth / PIXEL));
    var h = Math.max(1, Math.ceil(window.innerHeight / PIXEL));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
      gl.viewport(0, 0, w, h);
    }
  }

  function draw(seconds) {
    resize();
    gl.uniform2f(uRes, canvas.width, canvas.height);
    gl.uniform1f(uTime, seconds);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  function frame(now) {
    if (reduce && reduce.matches) { draw(still); return; }
    if (now - last >= 1000 / FPS - 1) {
      last = now;
      draw(still + (now - start) / 1000);
    }
    requestAnimationFrame(frame);
  }

  canvas.addEventListener('webglcontextlost', function (e) { e.preventDefault(); fail(); });
  window.addEventListener('resize', function () {
    if (reduce && reduce.matches) draw(still);
  });
  if (reduce && reduce.addEventListener) {
    reduce.addEventListener('change', function () { requestAnimationFrame(frame); });
  }
  requestAnimationFrame(frame);
})();
