const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { chromium } = require('playwright');
const { captureProfile } = require('./campervanfinder-profile.cjs');
const { validateSnapshot } = require('./campervanfinder-schema.cjs');

async function main() {
  const installed = process.env.LOCALAPPDATA && path.join(process.env.LOCALAPPDATA, 'Programs/Python/Python313/python.exe');
  const python = process.env.PYTHON || (installed && fs.existsSync(installed) ? installed : 'python');
  execFileSync(python, [path.join(__dirname, 'extract-campervanfinder-nz-reference.py')], { stdio: 'ignore' });
  const folder = path.resolve(__dirname, '../references/campervanfinder');
  const inventory = JSON.parse(fs.readFileSync(path.join(folder, 'nz-editable-fields.inventory.json'), 'utf8'));
  const browser = await chromium.launch({ channel: process.env.CAMPERVANFINDER_TEST_BROWSER || 'chrome', headless: true });
  try {
    const context = await browser.newContext({ javaScriptEnabled: false });
    await context.route('**/*', route => route.abort());
    const page = await context.newPage();
    for (const reference of inventory) {
      await page.setContent(fs.readFileSync(path.join(folder, reference.decodedFile), 'utf8'), { waitUntil: 'domcontentloaded' });
      const postId = Number(new URL(reference.editUrl).searchParams.get('post'));
      const snapshot = await captureProfile(page, postId, { country: 'nz', offline: true, editUrl: reference.editUrl,
        capturedAt: new Date(reference.savedAt).toISOString(), sourceFile: reference.file });
      validateSnapshot(snapshot, { country: 'nz', sample: true });
      const file = reference.decodedFile.replace('.snapshot.html', '.sample.json');
      fs.writeFileSync(path.join(folder, file), JSON.stringify(snapshot, null, 2) + '\n');
      console.log(`Reference sample: ${file}; product ${postId}; ${snapshot.data.vehicle.name}; ${Object.keys(snapshot.data.productTabs.tabs).length} tabs. sampleOnly=true`);
    }
  } finally { await browser.close(); }
}
if (require.main === module) main().catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { main };
