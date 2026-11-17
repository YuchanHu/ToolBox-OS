const { chromium } = require('C:/Users/Yuchan/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const fs = require('fs');
const path = require('path');
const source = fs.readFileSync('ui-preview/app.js', 'utf8');
const paths = Function(source.slice(0, source.indexOf('\nconst icon')) + ';return paths;')();
(async () => {
  const browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  const page = await browser.newPage();
  fs.mkdirSync('ui-preview/assets/icons', { recursive: true });
  for (const [name, data] of Object.entries(paths)) {
    if (process.argv.length > 2 && !process.argv.slice(2).includes(name)) continue;
    for (const size of [28, 78]) {
      await page.setContent(`<style>*{margin:0}svg{display:block}</style><svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="${size === 78 ? 1.4 : 1.8}" stroke-linecap="round" stroke-linejoin="round"><path d="${data}"/></svg>`);
      await page.locator('svg').screenshot({ path: path.join('ui-preview/assets/icons', `${name}-${size}.png`), omitBackground: true });
    }
  }
  await browser.close();
})();
