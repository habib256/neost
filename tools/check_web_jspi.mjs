#!/usr/bin/env node
// =============================================================================
//  check_web_jspi.mjs — le blit HOG long garé par JSPI, mesuré dans un vrai Chrome
//
//  Sur le web, un blit HOG de plusieurs secondes gare l'instruction qui l'a lancé
//  (core/EmuFiber.cpp, dorsal JSPI) : la page livre chaque trame close pendant le
//  blit et relance la pile quand la suivante est due (src/web/main_web.cpp,
//  neost_jspi_start_loop). Ce banc construit une disquette dont le secteur de boot
//  lance un blit HOG de 4 096 × N mots, la monte dans la page (wasm/index.html,
//  servie en local) et pilote Chrome sans écran par CDP. Il instrumente la page
//  par un script injecté AVANT son chargement — aucun crochet dans le code livré :
//    - chaque trame livrée pendant le blit : Module._neost_web_parked_frame enveloppé ;
//    - les tours de requestAnimationFrame : une boucle témoin, et sur demande un rAF
//      remplacé par un minuteur à N Hz (rAF bridé, comme un écran lent) ;
//    - le budget de rattrapage (Module.neostParkBudget) forçable à 0, pour comparer ;
//    - une exception levée dans la livraison d'une trame garée (la N-ième).
//
//  Scénarios (code 1 si un contrôle échoue) :
//    nominal  : STE 60 Hz (EmuTOS US) et 50 Hz (EmuTOS FR) sur le rAF du navigateur —
//               trames livrées ≈ durée × fréquence, aucun tour rAF bloqué ;
//    rattrapage : moniteur mono (71 Hz) sur le rAF ; rAF bridé à 30 et 10 Hz — avec le
//               budget, la cadence suit le temps émulé (plafond 4 trames par tour,
//               FramePacer::kMaxCatchUp) ; sans, elle tombe à celle du rAF ;
//    ordres   : reset, changement de ROM et de moniteur en plein blit (différés jusqu'au
//               déroulement de l'instruction garée), puis second blit mené à terme ;
//    ?fiber=0 : pas de JSPI, le blit d'un seul tenant (un tour rAF de plusieurs secondes) ;
//    exception : la page le dit et reste vivante, la machine s'arrête, et le bouton
//               Reset la relance (parked_ remis).
//
//  Prérequis (hors paliers de run_all : Chrome et le build Emscripten requis) :
//    emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DNEOST_WEB_FREE_ONLY=ON
//    cmake --build build-web -j --target neost-web        # → wasm/index.html
//    node tools/check_web_jspi.mjs [--only nom,nom] [--ycount N] [--verbose]
//  Variables : CHROME (binaire), NEOST_WASM_DIR (défaut wasm/). Node ≥ 22, ou Node 20
//  (le script se relance lui-même avec --experimental-websocket).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
// =============================================================================
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function argValue(name, def) {
  const i = process.argv.indexOf(name);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}

// -----------------------------------------------------------------------------
//  Disquette 720 Ko dont le secteur de boot (exécuté en superviseur par le TOS)
//  lance un blit HOG de xcount × ycount mots, puis compte en $600 — même programme
//  que tools/check_hog_park.py, adresses fixes (incréments nuls : rien n'est écrit
//  hors d'un mot, quelle que soit la taille de la RAM).
// -----------------------------------------------------------------------------
function hogDisk(xcount, ycount) {
  const code = [];
  const w = (...ws) => code.push(...ws);
  w(0x601C);                                        // bra.s $1E (saute le BPB)
  while (code.length < 0x1E / 2) w(0);
  w(0x41F9, 0x00FF, 0x8A00);                        // lea $FF8A00,a0
  const mvw = (v, off) => w(0x317C, v & 0xFFFF, off);
  const mvl = (v, off) => w(0x217C, (v >>> 16) & 0xFFFF, v & 0xFFFF, off);
  const mvb = (v, off) => w(0x117C, v & 0xFF, off);
  mvw(0, 0x20); mvw(0, 0x22); mvl(0x080000, 0x24);  // source : incréments nuls
  mvw(0xFFFF, 0x28); mvw(0xFFFF, 0x2A); mvw(0xFFFF, 0x2C);
  mvw(0, 0x2E); mvw(0, 0x30); mvl(0x0C0000, 0x32);  // destination : un seul mot
  mvw(xcount, 0x36); mvw(ycount, 0x38);
  mvb(2, 0x3A); mvb(3, 0x3B); mvb(0, 0x3D);         // HOP source, LOP D = S
  w(0x4E71); mvb(0xC0, 0x3C); w(0x4E71);            // BUSY|HOG
  w(0x52B8, 0x0600, 0x60FA);                        // addq.l #1,$600.w ; bra.s
  const img = new Uint8Array(1440 * 512);
  const dv = new DataView(img.buffer);
  code.forEach((x, i) => dv.setUint16(2 * i, x, false));
  dv.setUint16(0x0B, 512, true); img[0x0D] = 2; dv.setUint16(0x0E, 1, true);
  img[0x10] = 2; dv.setUint16(0x11, 112, true); dv.setUint16(0x13, 1440, true);
  img[0x15] = 0xF9; dv.setUint16(0x16, 5, true); dv.setUint16(0x18, 9, true);
  dv.setUint16(0x1A, 2, true);
  let sum = 0;                                      // secteur de boot exécutable : Σ mots = $1234
  for (let i = 0; i < 255; i++) sum = (sum + dv.getUint16(2 * i, false)) & 0xFFFF;
  dv.setUint16(0x1FE, (0x1234 - sum) & 0xFFFF, false);
  return img;
}

// -----------------------------------------------------------------------------
//  Instrumentation injectée avant le chargement de la page (cfg : rafHz, noBudget,
//  throwAt). Tout ce qui est mesuré vit dans window.__jt.
// -----------------------------------------------------------------------------
function probeSource(cfg) {
  return `(() => {
  const cfg = ${JSON.stringify(cfg)};
  const T = window.__jt = { parked: [], ticks: [], perTurn: [], cur: 0, thrown: 0 };
  if (cfg.rafHz) {
    // rAF bridé : les rappels sont servis par lots, à cfg.rafHz, comme un écran lent.
    let q = [];
    window.requestAnimationFrame = (cb) => { q.push(cb); return q.length; };
    setInterval(() => { const b = q; q = []; const t = performance.now();
                        for (const cb of b) cb(t); }, 1000 / cfg.rafHz);
  }
  const tick = () => {                // boucle témoin : un tour rAF = une entrée
    T.ticks.push(performance.now()); T.perTurn.push(T.cur); T.cur = 0;
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
  const iv = setInterval(() => {
    const M = window.Module;
    if (!M || typeof M._neost_web_parked_frame !== 'function') return;
    clearInterval(iv);
    const f = M._neost_web_parked_frame;
    M._neost_web_parked_frame = function () {
      T.parked.push(performance.now()); T.cur++;
      if (cfg.throwAt && T.parked.length === cfg.throwAt) {
        T.thrown++; throw new Error('injected fault in a parked frame');
      }
      return f.apply(this, arguments);
    };
    if (cfg.noBudget)
      Object.defineProperty(M, 'neostParkBudget', { get: () => 0, set: () => {}, configurable: true });
  }, 5);
})();`;
}

// -----------------------------------------------------------------------------
//  Serveur HTTP statique (application/wasm pour instantiateStreaming) et Chrome.
// -----------------------------------------------------------------------------
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
               '.data': 'application/octet-stream' };
function serve(dir) {
  return new Promise((res) => {
    const srv = http.createServer((req, rsp) => {
      const p = path.join(dir, decodeURIComponent(new URL(req.url, 'http://x').pathname));
      if (!p.startsWith(dir) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) {
        rsp.writeHead(404); rsp.end(); return;
      }
      rsp.writeHead(200, { 'Content-Type': MIME[path.extname(p)] || 'application/octet-stream' });
      fs.createReadStream(p).pipe(rsp);
    });
    srv.listen(0, '127.0.0.1', () => res(srv));
  });
}

function chromePath() {
  if (process.env.CHROME) return process.env.CHROME;
  const c = ['/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
             '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'];
  return c.find((p) => fs.existsSync(p));
}

async function launchChrome(bin, profile) {
  const proc = spawn(bin, ['--headless=new', '--remote-debugging-port=0',
    `--user-data-dir=${profile}`, '--autoplay-policy=no-user-gesture-required',
    '--no-first-run', '--no-default-browser-check', '--mute-audio',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'] });
  let err = '';
  const port = await new Promise((res, rej) => {
    proc.stderr.on('data', (d) => {
      err += d;
      const m = /DevTools listening on ws:\/\/[^:]+:(\d+)\//.exec(err);
      if (m) res(+m[1]);
    });
    proc.on('exit', () => rej(new Error('Chrome exited:\n' + err)));
  });
  return { proc, port };
}

class Cdp {
  static async open(port) {
    let list = [];
    for (let i = 0; i < 50 && !list.find((t) => t.type === 'page'); i++) {
      try { list = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch {}
      await sleep(100);
    }
    const page = list.find((t) => t.type === 'page');
    const c = new Cdp();
    c.ws = new WebSocket(page.webSocketDebuggerUrl);
    c.id = 0; c.pending = new Map(); c.logs = [];
    c.ws.onmessage = (e) => {
      const m = JSON.parse(e.data);
      if (m.id && c.pending.has(m.id)) { c.pending.get(m.id)(m); c.pending.delete(m.id); }
      else if (m.method === 'Runtime.consoleAPICalled')
        c.logs.push(m.params.args.map((a) => a.value ?? a.description).join(' '));
      else if (m.method === 'Runtime.exceptionThrown')
        c.logs.push('[uncaught] ' + (m.params.exceptionDetails.exception?.description
                                     ?? m.params.exceptionDetails.text));
    };
    await new Promise((r) => { c.ws.onopen = r; });
    await c.send('Runtime.enable');
    await c.send('Page.enable');
    return c;
  }
  send(method, params = {}) {
    return new Promise((r) => { const i = ++this.id; this.pending.set(i, r);
                                this.ws.send(JSON.stringify({ id: i, method, params })); });
  }
  async ev(expr) {
    const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true,
                                                    awaitPromise: true });
    if (r.result?.exceptionDetails) throw new Error(`${expr}: ${r.result.exceptionDetails.text}`);
    return r.result?.result?.value;
  }
}

// -----------------------------------------------------------------------------
//  Mesures
// -----------------------------------------------------------------------------
async function loadPage(cdp, base, query, cfg) {
  const ident = (await cdp.send('Page.addScriptToEvaluateOnNewDocument',
                                { source: probeSource(cfg) })).result.identifier;
  cdp.logs.length = 0;
  await cdp.send('Page.navigate', { url: `${base}/index.html${query}` });
  let ok = false;
  for (let i = 0; i < 300 && !ok; i++) {
    await sleep(100);
    try { ok = await cdp.ev('typeof ready !== "undefined" && ready === true'); } catch {}
  }
  await cdp.send('Page.removeScriptToEvaluateOnNewDocument', { identifier: ident });
  if (!ok) throw new Error('page not ready:\n' + cdp.logs.join('\n'));
  return await cdp.ev('Module.neostJspi === true');
}

async function writeDisk(cdp, img) {
  const b64 = Buffer.from(img).toString('base64');
  await cdp.ev(`(() => { const s = atob('${b64}'); const a = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) a[i] = s.charCodeAt(i);
    Module.FS.writeFile('/disks/hogblit.st', a); return 1; })()`);
}
const mount = (cdp, reset) =>
  cdp.ev(`Module.ccall('neost_mount_disk', 'number', ['string', 'number'], ['/disks/hogblit.st', ${reset}])`);
const loadTos = (cdp, rom) =>
  cdp.ev(`Module.ccall('neost_load_tos', 'number', ['string'], ['${rom}'])`);
const parkedCount = (cdp) => cdp.ev('__jt.parked.length');

// Attend une série de trames garées (un blit) : début, puis fin (silence de quietMs).
async function waitBlit(cdp, from = 0, { startMs = 30000, quietMs = 1500, maxMs = 60000 } = {}) {
  const t0 = Date.now();
  while ((await parkedCount(cdp)) <= from) {
    if (Date.now() - t0 > startMs) return null;
    await sleep(50);
  }
  let last = -1, lastChange = Date.now();
  while (Date.now() - t0 < maxMs) {
    const n = await parkedCount(cdp);
    if (n !== last) { last = n; lastChange = Date.now(); }
    else if (Date.now() - lastChange > quietMs) break;
    await sleep(50);
  }
  return blitStats(await cdp.ev('__jt'), from);
}

function blitStats(jt, from) {
  const p = jt.parked.slice(from);
  const t0 = p[0], t1 = p[p.length - 1];
  // Tours rAF pendant le blit : écarts entre tours de la boucle témoin, et trames
  // livrées par tour (comptées entre deux tours consécutifs).
  let maxGap = 0, maxPerTurn = 0;
  const hist = {};
  for (let i = 1; i < jt.ticks.length; i++) {
    if (jt.ticks[i] < t0 || jt.ticks[i - 1] > t1) continue;
    maxGap = Math.max(maxGap, jt.ticks[i] - jt.ticks[i - 1]);
  }
  for (let i = 0; i < jt.ticks.length; i++) {
    if (jt.ticks[i] < t0 || jt.ticks[i] > t1 + 200) continue;
    const k = jt.perTurn[i];
    hist[k] = (hist[k] || 0) + 1;
    maxPerTurn = Math.max(maxPerTurn, k);
  }
  const ms = t1 - t0;
  return { frames: p.length, ms, fps: ms > 0 ? (p.length - 1) * 1000 / ms : 0,
           maxGap, maxPerTurn, hist };
}

function fmt(s) {
  if (!s) return 'no parked frame';
  const h = Object.entries(s.hist).map(([k, v]) => `${k}:${v}`).join(' ');
  return `${s.frames} frames in ${(s.ms / 1000).toFixed(2)} s = ${s.fps.toFixed(1)} fps, ` +
         `max rAF gap ${s.maxGap.toFixed(1)} ms, frames/turn max ${s.maxPerTurn} {${h}}`;
}

// Boucle témoin sans JSPI (?fiber=0) : le plus long tour rAF après le montage.
async function maxGapAfter(cdp, t0, ms) {
  await sleep(ms);
  return await cdp.ev(`(() => { const t = __jt.ticks; let m = 0;
    for (let i = 1; i < t.length; i++) if (t[i - 1] >= ${t0}) m = Math.max(m, t[i] - t[i - 1]);
    return m; })()`);
}

// -----------------------------------------------------------------------------
async function main() {
  const wasmDir = path.resolve(process.env.NEOST_WASM_DIR || path.join(ROOT, 'wasm'));
  const verbose = process.argv.includes('--verbose');
  const only = (argValue('--only', '') || '').split(',').filter(Boolean);
  const ycount = +argValue('--ycount', '1000');
  if (!fs.existsSync(path.join(wasmDir, 'index.html'))) {
    console.error(`SKIP: ${wasmDir}/index.html missing (build the neost-web target)`); return 1;
  }
  const bin = chromePath();
  if (!bin) { console.error('SKIP: Chrome not found (set CHROME)'); return 1; }

  const srv = await serve(wasmDir);
  const base = `http://127.0.0.1:${srv.address().port}`;
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'neost-jspi-'));
  const { proc, port } = await launchChrome(bin, profile);
  const cdp = await Cdp.open(port);
  const ver = (await cdp.send('Browser.getVersion')).result?.product;
  console.log(`[web-jspi] ${ver}, blit 4096 x ${ycount} words, page ${base}`);
  const disk = hogDisk(4096, ycount);

  const fails = [];
  const check = (ok, what) => {
    console.log(`[web-jspi]   ${what.padEnd(70)} ${ok ? 'OK' : 'FAIL'}`);
    if (!ok) fails.push(what);
  };
  const near = (fps, hz, tol = 0.04) => Math.abs(fps - hz) <= hz * tol;
  const want = (name) => only.length === 0 || only.includes(name);
  const logsHave = (re) => cdp.logs.some((l) => re.test(l));

  // Une page, un blit : rend les statistiques (null : pas de blit garé).
  async function blit(name, query, cfg, setup) {
    const jspi = await loadPage(cdp, base, query, cfg);
    await writeDisk(cdp, disk);
    await setup();
    const s = await waitBlit(cdp);
    console.log(`[web-jspi] ${name}: ${fmt(s)}${jspi ? '' : ' (no JSPI)'}`);
    if (verbose) console.log(cdp.logs.join('\n'));
    return s;
  }
  const bootHog = async () => { await mount(cdp, 1); };

  try {
    if (want('nominal')) {
      const s60 = await blit('nominal 60 Hz', '?machine=ste', {}, bootHog);
      check(s60 && near(s60.fps, 60), 'STE 60 Hz: delivered frames follow emulated time');
      check(s60 && s60.maxGap < 34, 'STE 60 Hz: no rAF turn blocked by the blit (< 34 ms)');
      check(s60 && s60.maxPerTurn <= 4, 'STE 60 Hz: at most 4 frames per rAF turn');
      const s50 = await blit('nominal 50 Hz', '?machine=ste', {}, async () => {
        await mount(cdp, 0); await loadTos(cdp, '/roms/etos256fr.img'); });
      check(s50 && near(s50.fps, 50), 'STE 50 Hz (EmuTOS FR): delivered frames follow emulated time');
      check(s50 && s50.maxGap < 34, 'STE 50 Hz: no rAF turn blocked by the blit');
    }
    if (want('catchup')) {
      const mono = async () => {
        await mount(cdp, 0); await cdp.ev("Module.ccall('neost_set_mono', null, ['number'], [1])"); };
      const m71 = await blit('mono 71 Hz, budget', '?machine=ste', {}, mono);
      const m71n = await blit('mono 71 Hz, no budget', '?machine=ste', { noBudget: true }, mono);
      check(m71 && near(m71.fps, 71.4), 'mono 71 Hz on the browser rAF: catch-up keeps 71 Hz');
      check(m71n && m71n.fps < 64, 'mono 71 Hz without the budget: capped by the rAF (control)');
      const r30 = await blit('rAF 30 Hz, budget', '?machine=ste', { rafHz: 30 }, bootHog);
      const r30n = await blit('rAF 30 Hz, no budget', '?machine=ste', { rafHz: 30, noBudget: true }, bootHog);
      check(r30 && near(r30.fps, 60), 'rAF 30 Hz: catch-up keeps the machine at 60 Hz');
      check(r30 && r30.maxPerTurn <= 4, 'rAF 30 Hz: at most 4 frames per turn');
      check(r30n && near(r30n.fps, 30, 0.08), 'rAF 30 Hz without the budget: 30 Hz (control)');
      const r10 = await blit('rAF 10 Hz, budget', '?machine=ste', { rafHz: 10 }, bootHog);
      check(r10 && r10.maxPerTurn === 4, 'rAF 10 Hz: catch-up capped at 4 frames per turn (kMaxCatchUp)');
      check(r10 && near(r10.fps, 40, 0.08), 'rAF 10 Hz: 40 Hz, the lag beyond the cap is dropped');
    }
    if (want('orders')) {
      // Reset, ROM, moniteur en plein blit : l'ordre attend le déroulement de
      // l'instruction garée, la machine redémarre et le second blit va à son terme.
      const orders = [
        ['reset', "Module._neost_reset()", 60],
        ['hard reset', "Module._neost_hard_reset()", 60],
        ['ROM change (EmuTOS FR)', "Module.ccall('neost_load_tos', 'number', ['string'], ['/roms/etos256fr.img'])", 50],
        ['mono monitor', "Module.ccall('neost_set_mono', null, ['number'], [1])", 71.4],
      ];
      for (const [what, call, hz] of orders) {
        await loadPage(cdp, base, '?machine=ste', {});
        await writeDisk(cdp, disk);
        await mount(cdp, 1);
        const t0 = Date.now();
        while ((await parkedCount(cdp)) < 30 && Date.now() - t0 < 30000) await sleep(20);
        // L'ordre et le relevé dans le même appel : sous charge, deux allers-retours
        // CDP laissent passer des trames entre les deux.
        const [n0, tc] = await cdp.ev(`(() => { const n = __jt.parked.length,
          t = performance.now(); ${call}; return [n, t]; })()`);
        // Trames encore livrées dans la seconde qui suit (le TOS met plus de 2 s à
        // relancer le blit) : 0 si l'ordre a déroulé l'instruction garée sur-le-champ.
        await sleep(1000);
        const late = await cdp.ev(`__jt.parked.filter((t) => t > ${tc} && t < ${tc} + 1000).length`);
        const n1 = await parkedCount(cdp);
        const s = await waitBlit(cdp, n1, { quietMs: 1500 });
        console.log(`[web-jspi] ${what} at parked frame ${n0}: ${late} frame(s) after it; ` +
                    `second blit ${fmt(s)}`);
        if (verbose) {
          const p = await cdp.ev('__jt.parked');
          console.log('  parked frames after the order (ms): ' +
                      p.slice(n0 - 2, n0 + 8).map((t) => (t - tc).toFixed(1)).join(' '));
          console.log(cdp.logs.join('\n'));
        }
        check(logsHave(/parked instruction abandoned/), `${what} mid-blit: parked instruction abandoned`);
        check(late === 0, `${what} mid-blit: applied at once (no parked frame after it)`);
        check(s && s.frames > 100 && near(s.fps, hz), `${what}: machine reboots, second blit at ${hz} Hz`);
      }
    }
    if (want('fiber0')) {
      const jspi = await loadPage(cdp, base, '?machine=ste&fiber=0', {});
      await writeDisk(cdp, disk);
      const t0 = await cdp.ev('performance.now()');
      await mount(cdp, 1);
      const gap = await maxGapAfter(cdp, t0, 12000);
      const n = await parkedCount(cdp);
      console.log(`[web-jspi] ?fiber=0: JSPI ${jspi}, ${n} parked frames, longest rAF turn ${gap.toFixed(0)} ms`);
      check(!jspi && n === 0, '?fiber=0: no JSPI, no parked frame');
      check(gap > 200, '?fiber=0: the blit runs in one piece (one long rAF turn)');
    }
    if (want('exception')) {
      // Une exception pendant la livraison d'une trame garée remonte jusqu'à la page.
      // Attendu : la page le dit et reste vivante (rAF, boutons), la machine s'arrête,
      // et le bouton Reset la relance sur-le-champ — parked_ a été remis (garde RAII
      // d'EmuFiber::yield), le reset n'est donc pas différé à jamais.
      await loadPage(cdp, base, '?machine=ste', { throwAt: 30 });
      await writeDisk(cdp, disk);
      await mount(cdp, 1);
      const t0 = Date.now();
      while ((await cdp.ev('__jt.thrown')) === 0 && Date.now() - t0 < 30000) await sleep(20);
      await sleep(1000);
      const status = await cdp.ev("document.getElementById('status').textContent");
      const ticks0 = await cdp.ev('__jt.ticks.length');
      await sleep(500);
      const ticks1 = await cdp.ev('__jt.ticks.length');
      const n0 = await parkedCount(cdp);
      console.log(`[web-jspi] exception at parked frame 30: status "${status}", ` +
                  `${ticks1 - ticks0} rAF turns in 0.5 s, ${n0 - 30} parked frame(s) after it`);
      check(/stopped|failed|crash/i.test(status), 'exception: the page says the frame failed');
      check(ticks1 - ticks0 > 10, 'exception: the page stays alive (rAF keeps turning)');
      check(n0 === 30, 'exception: the machine stops (nothing runs on a cut instruction)');
      cdp.logs.length = 0;
      await cdp.ev("document.getElementById('btnReset').click()");
      const s = await waitBlit(cdp, n0, { startMs: 20000 });
      const st2 = await cdp.ev("document.getElementById('status').textContent");
      console.log(`[web-jspi] after Reset: status "${st2}", ${fmt(s)}`);
      if (verbose) console.log(cdp.logs.join('\n'));
      check(!logsHave(/parked instruction abandoned/), 'exception: parked_ cleared, Reset not deferred');
      check(s && s.frames > 100 && near(s.fps, 60), 'exception: Reset restarts the machine (second blit)');
    }
  } finally {
    cdp.ws.close();
    proc.kill();
    srv.close();
    try { fs.rmSync(profile, { recursive: true, force: true }); } catch {}
  }
  console.log(`[web-jspi] ${fails.length ? 'FAILED: ' + fails.join(', ') : 'all green'}`);
  return fails.length ? 1 : 0;
}

if (typeof WebSocket === 'undefined') {           // Node 20 : WebSocket derrière un drapeau
  const r = spawn(process.execPath, ['--experimental-websocket', '--no-warnings',
    fileURLToPath(import.meta.url), ...process.argv.slice(2)], { stdio: 'inherit' });
  r.on('exit', (c) => process.exit(c ?? 1));
} else {
  main().then((c) => process.exit(c), (e) => { console.error(e); process.exit(1); });
}
