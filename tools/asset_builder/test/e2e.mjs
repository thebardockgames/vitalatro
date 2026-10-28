// End-to-end test of the site in headless Chromium (Playwright), WSL only, no window:
//   node e2e.mjs <site url> <game file> <out.vpk> [<screenshot dir>]
// Picks the file, waits for the version check, builds, downloads the VPK.
import { chromium } from 'playwright';

const [url, gameFile, outVpk, shots] = process.argv.slice(2);
const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
const ctx = await browser.newContext({ viewport: { width: 1100, height: 1000 }, acceptDownloads: true });
const page = await ctx.newPage();
page.on('console', (m) => console.log('console:', m.text()));
page.on('pageerror', (e) => console.log('pageerror:', e.message));
const t0 = Date.now();
await page.goto(url);
await page.setInputFiles('#file', gameFile);
await page.waitForSelector('#file-status:not([hidden])');
await page.waitForFunction(() => {
  const s = document.querySelector('#file-status');
  return s.classList.contains('ok') || s.classList.contains('err');
}, null, { timeout: 120000 });
const check = await page.textContent('#file-status');
console.log('check:', check);
if (shots) await page.screenshot({ path: `${shots}/checked.png` });
if (!(await page.isEnabled('#build'))) { await browser.close(); process.exit(2); }
await page.click('#build');
let last = '';
const timer = setInterval(async () => {
  try {
    const t = (await page.textContent('#progress-text')) + ' ' + (await page.textContent('#progress-pct'));
    if (t !== last) { console.log('progress:', t); last = t; }
  } catch (e) { /* page closing */ }
}, 500);
await page.waitForSelector('#download:not([hidden])', { timeout: 600000 }).catch(async (e) => {
  console.log('status:', await page.textContent('#build-status'));
  throw e;
});
clearInterval(timer);
console.log('built in', ((Date.now() - t0) / 1000).toFixed(1), 's:', await page.textContent('#build-status'));
if (shots) await page.screenshot({ path: `${shots}/done.png` });
const [download] = await Promise.all([page.waitForEvent('download'), page.click('#download')]);
console.log('download name:', download.suggestedFilename());
await download.saveAs(outVpk);
await browser.close();
