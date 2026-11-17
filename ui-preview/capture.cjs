const { chromium } = require('C:/Users/Yuchan/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const { pathToFileURL } = require('node:url');
const path = require('node:path');
(async () => {
  const browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  const page = await browser.newPage({ viewport: { width: 1560, height: 2400 }, deviceScaleFactor: 1 });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(pathToFileURL(path.join(__dirname, 'index.html')).href + '?overview=1');
  await page.evaluate(() => document.fonts.ready);
  for (const id of ['boot','home','analog','faces','drawer','clock','control','wifi','ap','settings','stopwatch','phone']) {
    await page.locator(`#${id} > div`).screenshot({ path: path.join(__dirname, 'screenshots', id + '.png') });
  }
  await page.screenshot({ path: path.join(__dirname, 'screenshots', 'overview.png'), fullPage: true });
  const lock = page.locator('#analog > div');
  await lock.scrollIntoViewIfNeeded();
  const bounds = await lock.boundingBox();
  const x = bounds.x + 233;
  const y = bounds.y + 300;
  await page.mouse.move(x, y);
  await page.mouse.down();
  for (let step = 1; step <= 8; step++) {
    await page.mouse.move(x, y - 60 * step / 8);
    await page.waitForTimeout(60);
  }
  const opacity = await lock.locator('.unlock-drawer').evaluate(el => Number(el.style.opacity));
  if (opacity <= 0 || opacity >= 1) throw Error('Unlock does not follow pointer');
  await lock.screenshot({ path: path.join(__dirname, 'screenshots', 'unlock-drag.png') });
  await page.mouse.up();
  await page.waitForTimeout(380);
  if (await lock.getAttribute('data-page') !== 'analog') throw Error('Unlock rebound failed');
  await page.mouse.move(x, y);
  await page.mouse.down();
  await page.mouse.move(x, y - 160, { steps: 12 });
  await page.mouse.up();
  await page.waitForTimeout(380);
  if (await lock.getAttribute('data-page') !== 'drawer') throw Error('Unlock completion failed');
  await page.mouse.move(x, y);
  await page.mouse.down();
  await page.mouse.move(x, y - 160, { steps: 12 });
  await page.mouse.up();
  if (await lock.getAttribute('data-page') !== 'drawer') throw Error('Drawer upward swipe changed page');
  await page.locator('#home [data-go="drawer"]').click();
  if (await page.locator('#home .app-name').textContent() !== '秒表') throw Error('Drawer failed');
  await page.locator('#home [data-step="-1"]').click();
  if (await page.locator('#home .app-name').textContent() !== '时钟') throw Error('Loop failed');
  await page.locator('#stopwatch [data-run]').click();
  await page.waitForTimeout(150);
  if (await page.locator('#stopwatch .stop-fraction').textContent() === '.00') throw Error('Timer failed');
  await page.locator('#phone #password').fill('sample123');
  await page.locator('#phone button').click();
  if (!(await page.locator('#result').textContent()).includes('连接成功')) throw Error('Form failed');
  await page.locator('#faces [data-face="analog"]').click();
  if (await page.locator('#faces .analog-dial').count() !== 1) throw Error('Analog selection failed');
  await page.locator('#faces [data-go="drawer"]').click();
  await page.locator('#faces .back').click();
  if (await page.locator('#faces .analog-dial').count() !== 1) throw Error('Selected face return failed');
  console.log(JSON.stringify({ errors, fonts: await page.evaluate(() => document.fonts.check('24px MiSans')), screenshots: 13, interactions: 'drawer, loop, stopwatch, provisioning form, analog selection and return passed' }));
  await page.goto(pathToFileURL(path.join(__dirname, 'index.html')).href + '?page=home');
  async function swipe(from, to) {
    await page.mouse.move(233, from);
    await page.mouse.down();
    await page.mouse.move(233, to, { steps: 10 });
    await page.mouse.up();
  }
  await swipe(20, 120);
  if (await page.locator('.clock-layer').count() !== 1) throw Error('First pull failed');
  await page.waitForTimeout(1800);
  if (await page.locator('.clock-layer').count()) throw Error('First pull timeout failed');
  await swipe(20, 120);
  await swipe(40, 140);
  await page.waitForTimeout(1800);
  if (await page.locator('.cc').count() !== 1) throw Error('Control should remain open');
  await swipe(400, 280);
  await page.waitForTimeout(250);
  if (await page.locator('.cc,.clock-layer').count()) throw Error('Control must dismiss directly');
  console.log('Browser overlay timeout and direct dismissal checks passed');
  await browser.close();
})();
