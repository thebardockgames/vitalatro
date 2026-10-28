// Animated page background: the game's main menu swirl (see FS below).
// Static single frame with prefers-reduced-motion; CSS gradient fallback without WebGL.
(function () {
  'use strict';
  var canvas = document.getElementById('bg');
  var FPS = 60;

  // The game's main menu background (resources/shaders/splash.fs, as Game:main_menu draws it on
  // G.SPLASH_BACK: vort_speed 0.4, colour_1 = G.C.RED, colour_2 = G.C.BLUE, vort_offset 0), ported
  // to WebGL 1. LOVE's screen_coords start at the top left, so y is flipped from gl_FragCoord.
  // mid_flash (the white flash when the menu opens) is left at its settled value, 0.
  var VS = 'attribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }';
  var FS = [
    '#ifdef GL_FRAGMENT_PRECISION_HIGH',
    'precision highp float;',
    '#else',
    'precision mediump float;',
    '#endif',
    'uniform vec2 uRes;',
    'uniform float time;',
    'const float vort_speed = 0.4;',
    'const vec4 colour_1 = vec4(254.0 / 255.0, 95.0 / 255.0, 85.0 / 255.0, 1.0);',
    'const vec4 colour_2 = vec4(0.0, 157.0 / 255.0, 1.0, 1.0);',
    'const float mid_flash = 0.0;',
    'const float vort_offset = 0.0;',
    '#define PIXEL_SIZE_FAC 700.',
    '#define BLACK 0.6*vec4(79./255.,99./255., 103./255., 1./0.6)',
    'void main() {',
    '  vec2 screen_coords = vec2(gl_FragCoord.x, uRes.y - gl_FragCoord.y);',
    '  float pixel_size = length(uRes)/PIXEL_SIZE_FAC;',
    '  vec2 uv = (floor(screen_coords*(1./pixel_size))*pixel_size - 0.5*uRes)/length(uRes);',
    '  float uv_len = length(uv);',
    '  float speed = time*vort_speed;',
    '  float new_pixel_angle = atan(uv.y, uv.x) + (2.2 + 0.4*min(6.,speed))*uv_len - 1. - speed*0.05 - min(6.,speed)*speed*0.02 + vort_offset;',
    '  vec2 mid = (uRes/length(uRes))/2.;',
    '  vec2 sv = vec2((uv_len * cos(new_pixel_angle) + mid.x), (uv_len * sin(new_pixel_angle) + mid.y)) - mid;',
    '  sv *= 30.;',
    '  speed = time*(6.)*vort_speed + vort_offset + 1033.;',
    '  vec2 uv2 = vec2(sv.x+sv.y);',
    '  for (int i = 0; i < 5; i++) {',
    '    uv2 += sin(max(sv.x, sv.y)) + sv;',
    '    sv += 0.5*vec2(cos(5.1123314 + 0.353*uv2.y + speed*0.131121), sin(uv2.x - 0.113*speed));',
    '    sv -= 1.0*cos(sv.x + sv.y) - 1.0*sin(sv.x*0.711 - sv.y);',
    '  }',
    '  float smoke_res = min(2., max(-2., 1.5 + length(sv)*0.12 - 0.17*(min(10.,time*1.2 - 4.))));',
    '  if (smoke_res < 0.2) smoke_res = (smoke_res - 0.2)*0.6 + 0.2;',
    '  float c1p = max(0.,1. - 2.*abs(1.-smoke_res));',
    '  float c2p = max(0.,1. - 2.*(smoke_res));',
    '  float cb = 1. - min(1., c1p + c2p);',
    '  vec4 ret_col = colour_1*c1p + colour_2*c2p + vec4(cb*BLACK.rgb, cb*colour_1.a);',
    '  float mod_flash = max(mid_flash*0.8, max(c1p, c2p)*5. - 4.4) + mid_flash*max(c1p, c2p);',
    '  gl_FragColor = vec4((ret_col*(1. - mod_flash) + mod_flash*vec4(1., 1., 1., 1.)).rgb, 1.0);',
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
  var uTime = gl.getUniformLocation(prog, 'time');

  var reduce = window.matchMedia ? window.matchMedia('(prefers-reduced-motion: reduce)') : null;
  var start = performance.now();
  var last = 0;
  // The game holds the shader clock at 300 with reduced motion; otherwise start past the intro
  // (the swirl and smoke ramps settle by ~15 s) so the page opens on the settled menu look.
  var still = 300;
  var intro = 20;

  function resize() {
    // Full CSS-pixel resolution: the shader makes its own pixels (screen diagonal / 700).
    var w = Math.max(1, Math.ceil(window.innerWidth));
    var h = Math.max(1, Math.ceil(window.innerHeight));
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
      draw(intro + (now - start) / 1000);
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
