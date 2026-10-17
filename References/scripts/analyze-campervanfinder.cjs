const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { chromium } = require('playwright');
const { captureProfile } = require('./campervanfinder-profile.cjs');
const { validateSnapshot } = require('./campervanfinder-schema.cjs');
const root = path.resolve(__dirname, '..');

async function main() {
  const installedPython = process.env.LOCALAPPDATA && path.join(process.env.LOCALAPPDATA, 'Programs', 'Python', 'Python313', 'python.exe');
  const python = process.env.PYTHON || (installedPython && fs.existsSync(installedPython) ? installedPython : 'python');
  execFileSync(python, [path.join(__dirname, 'extract-campervanfinder-reference.py')], { stdio: 'inherit' });
  const folder = path.join(root, 'references', 'campervanfinder');
  const reference = JSON.parse(fs.readFileSync(path.join(folder, 'reference-capture.json'), 'utf8'));
  const browser = await chromium.launch({ channel: process.env.CAMPERVANFINDER_TEST_BROWSER || 'chrome', headless: true });
  try {
    const context = await browser.newContext({ javaScriptEnabled: false });
    await context.route('**/*', route => route.abort());
    const page = await context.newPage();
    await page.setContent(fs.readFileSync(path.join(folder, 'vehicle-editor.snapshot.html'), 'utf8'), { waitUntil: 'domcontentloaded' });
    const postId = Number(new URL(reference.editUrl).searchParams.get('post'));
    const snapshot = await captureProfile(page, postId, { offline: true, editUrl: reference.editUrl, capturedAt: new Date(reference.savedAt).toISOString(), sourceFile: reference.file });
    validateSnapshot(snapshot, { sample: true });
    snapshot.audit.tabTables = await page.evaluate(tabs => {
      const result = {};
      for (const [index, tab] of Object.entries(tabs)) {
        if (!tab.content) continue;
        const document = new DOMParser().parseFromString(tab.content, 'text/html');
        result[index] = [...document.querySelectorAll('table.shop_attributes tr')].map(row => ({ label: row.querySelector('th')?.textContent.trim(), valueHtml: row.querySelector('td')?.innerHTML }));
      }
      return result;
    }, snapshot.data.productTabs.tabs);
    const file = path.join(folder, 'vehicle-profile-editor.sample.json');
    fs.writeFileSync(file, JSON.stringify(snapshot, null, 2) + '\n');
    console.log(`Sample saved: ${file}`);
  } finally { await browser.close(); }
}

if (require.main === module) main().catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { main };
