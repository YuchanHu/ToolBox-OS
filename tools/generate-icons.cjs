const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const preview = path.join(__dirname, '..', 'ui-preview');
const output = path.join(preview, 'assets', 'icons');
const source = fs.readFileSync(path.join(preview, 'app.js'), 'utf8');
const paths = Function(source.slice(0, source.indexOf('\nconst icon')) + ';return paths;')();
(async () => {
  const browser = await chromium.launch({ executablePath: process.env.TOOLBOX_CHROMIUM_PATH, headless: true });
  const page = await browser.newPage();
  fs.mkdirSync(output, { recursive: true });
  for (const [name, data] of Object.entries(paths)) {
    if (process.argv.length > 2 && !process.argv.slice(2).includes(name)) continue;
    for (const size of [28, 78]) {
      await page.setContent(`<style>*{margin:0}svg{display:block}</style><svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="${size === 78 ? 1.4 : 1.8}" stroke-linecap="round" stroke-linejoin="round"><path d="${data}"/></svg>`);
      await page.locator('svg').screenshot({ path: path.join(output, `${name}-${size}.png`), omitBackground: true });
    }
  }
  await browser.close();
})();
