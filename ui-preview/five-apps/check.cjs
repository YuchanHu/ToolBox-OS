const { chromium } = require('C:/Users/Yuchan/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const { pathToFileURL } = require('node:url');
const path = require('node:path');
(async () => {
  const browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  const page = await browser.newPage({ viewport: { width: 1540, height: 1500 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(pathToFileURL(path.join(__dirname, 'interaction.html')).href);
  await page.evaluate(() => document.fonts.ready);
  for (const id of ['level','focus','timers']) {
    const watch=page.locator('#'+id);
    if(await watch.locator('.controls').evaluate(el=>getComputedStyle(el).opacity)!=='0') throw Error('Controls initially visible');
    await watch.click();
    await page.waitForTimeout(350);
    if(await watch.locator('.controls').evaluate(el=>getComputedStyle(el).opacity)!=='1') throw Error('Reveal failed');
    await watch.screenshot({path:path.join(__dirname,id+'-revealed.png')});
    await page.waitForTimeout(2100);
    if(await watch.locator('.controls').evaluate(el=>getComputedStyle(el).opacity)!=='0') throw Error('Fade failed');
  }
  for(const value of await page.locator('option').evaluateAll(options=>options.map(o=>o.value))){
    await page.selectOption('select',value);
    await page.waitForTimeout(100);
    await page.locator('#weather').screenshot({path:path.join(__dirname,'weather-motion-'+value+'.png')});
  }
  await page.selectOption('select','clear');
  await page.check('#night');
  await page.locator('#weather').screenshot({path:path.join(__dirname,'weather-motion-night.png')});
  await page.uncheck('#night');
  await page.screenshot({path:path.join(__dirname,'interaction-overview.png'),fullPage:true});
  if(errors.length)throw Error(errors.join('\n'));
  await browser.close();
  console.log('Passed default hidden, click reveal, 2-second timeout and fade for 3 apps; 13 weather states and night rendered.');
})().catch(error=>{console.error(error);process.exit(1)});
