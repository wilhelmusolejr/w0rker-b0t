const fs = require('node:fs');
const { SITES } = require('./catalog-fields.cjs');
const project = require('./project.cjs');
const { inside } = require('./catalog-store.cjs');
const { connectProfile } = require('./hidemium.cjs');

const DEFAULT_LIST = 'jobs/open-vehicle-tabs.txt';

function parseArgs(args) {
  const options = { urls: [] };
  for (let i = 0; i < args.length; i++) {
    const argument = args[i];
    if (argument === '--help') options.help = true;
    else if (['--file', '--site'].includes(argument)) {
      const key = argument.slice(2);
      if (options[key] || !args[i + 1] || args[i + 1].startsWith('--')) throw new Error(`Missing or duplicate value for ${argument}.`);
      options[key] = args[++i];
    } else if (argument.startsWith('-')) throw new Error(`Unknown option: ${argument}.`);
    else options.urls.push(argument);
  }
  if (options.help) return options;
  if (options.site && !Object.hasOwn(SITES, options.site)) throw new Error(`Unknown site: ${options.site}. Sites: ${Object.keys(SITES).join(', ')}.`);
  if (options.file && options.urls.length) throw new Error('Use --file or URL arguments, not both.');
  return options;
}

function planTabs(options, workspace = project.root) {
  let urls = options.urls;
  if (options.file || !urls.length) {
    const filename = options.file || DEFAULT_LIST;
    const file = inside(workspace, filename);
    if (!fs.existsSync(file)) throw new Error(`URL list not found: ${filename}. Copy jobs/open-vehicle-tabs.example.txt to ${DEFAULT_LIST} and add one URL per line.`);
    urls = fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '').split(/\r?\n/).map(line => line.trim()).filter(line => line && !line.startsWith('#'));
  }
  if (!urls.length) throw new Error('The vehicle URL list is empty. Add one full public or editor URL per line.');

  // Validate the whole list and every configuration before opening any browser.
  const targets = urls.map(value => {
    let url;
    try { url = new URL(value); } catch { throw new Error('Each vehicle needs a full HTTP or HTTPS URL.'); }
    if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password || /\s/.test(value)) throw new Error('Use HTTP/HTTPS vehicle URLs without spaces or embedded credentials.');
    const siteId = options.site || Object.keys(SITES).find(id => new URL(SITES[id].origin).host === url.host);
    if (!siteId) throw new Error(`No configured site matches ${url.host}. Use --site SITE_ID to choose the Hidemium browser for this URL.`);
    return { siteId, url: value };
  });

  const configs = new Map();
  const groups = new Map();
  for (const target of targets) {
    if (!configs.has(target.siteId)) configs.set(target.siteId, project.readSiteConfig(target.siteId, workspace));
    const config = configs.get(target.siteId);
    const apiUrl = config.hidemiumApiUrl || process.env.HIDEMIUM_API_URL || 'http://127.0.0.1:2222';
    const timeout = config.pageTimeoutMs ?? 60_000;
    if (!Number.isSafeInteger(timeout) || timeout <= 0) throw new Error(`Invalid pageTimeoutMs for ${target.siteId}.`);
    const api = new URL(apiUrl);
    if (!['http:', 'https:'].includes(api.protocol) || api.username || api.password) throw new Error(`Invalid Hidemium API URL for ${target.siteId}.`);
    const profileId = config.hidemiumProfileId;
    const key = JSON.stringify([api.origin, profileId]);
    if (!groups.has(key)) groups.set(key, { profileId, apiUrl, targets: [] });
    groups.get(key).targets.push({ ...target, timeout });
  }
  return [...groups.values()];
}

async function openTabs(groups, log = console.log) {
  const result = { opened: 0, failures: [] };
  for (const group of groups) {
    let browser;
    try {
      browser = await connectProfile({ profileId: group.profileId, apiUrl: group.apiUrl, closeOnConnectFailure: false });
    } catch (error) {
      for (const target of group.targets) result.failures.push({ url: target.url, error: error.message });
      log(`Could not connect to Hidemium: ${error.message}`);
      continue;
    }

    try {
      const context = browser.contexts()[0];
      if (!context) throw new Error('No Hidemium browser context found.');
      let lastPage;
      for (const target of group.targets) {
        let page;
        try {
          page = await context.newPage();
          lastPage = page;
          result.opened++;
          // Once navigation starts, let the browser finish loading for the user.
          await page.goto(target.url, { waitUntil: 'commit', timeout: target.timeout });
          log(`Opened: ${target.url}`);
        } catch (error) {
          result.failures.push({ url: target.url, error: error.message });
          log(`${page ? 'Tab left open; could not load' : 'Could not open'}: ${target.url} (${error.message})`);
        }
      }
      if (lastPage && !lastPage.isClosed()) await lastPage.bringToFront();
    } catch (error) {
      result.failures.push({ error: error.message });
      log(error.message);
    } finally {
      // This browser came from connectOverCDP: close disconnects this client.
      // Do not close pages/contexts or call Hidemium's closeProfile API.
      try { await browser.close(); }
      catch (error) {
        result.failures.push({ error: `Could not disconnect from Hidemium: ${error.message}` });
        log(result.failures.at(-1).error);
      }
    }
  }
  log(`Opened ${result.opened} new tab${result.opened === 1 ? '' : 's'}. Hidemium and the tabs stay open for manual work.${result.failures.length ? ` ${result.failures.length} problem(s); review the messages above.` : ''}`);
  return result;
}

async function main(args = process.argv.slice(2), workspace = project.root) {
  const options = parseArgs(args);
  if (options.help) {
    console.log('Open vehicle tabs for manual work and leave Hidemium running:\n  node scripts/open-vehicle-tabs.cjs\n  node scripts/open-vehicle-tabs.cjs --file jobs/open-vehicle-tabs.txt\n  node scripts/open-vehicle-tabs.cjs "URL_1" "URL_2"\n  node scripts/open-vehicle-tabs.cjs --site SITE_ID --file jobs/open-vehicle-tabs.txt\n\nDefault list: ' + DEFAULT_LIST + ' (one full URL per line; blank lines and # comments ignored).\nManaged URLs select their configured browser automatically. --site opens every URL in that site\'s browser, including external sources. Existing tabs remain open. URLs open exactly as supplied.\nSites: ' + Object.keys(SITES).join(', '));
    return;
  }
  return openTabs(planTabs(options, workspace));
}

if (require.main === module) main().then(result => { if (result?.failures.length) process.exitCode = 1; }).catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { parseArgs, planTabs, main };
