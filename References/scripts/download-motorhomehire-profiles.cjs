const fs = require('node:fs');
const path = require('node:path');
const { root, readSiteConfig } = require('./project.cjs');
const { connectProfile, prepareAutomationTab, closeAutomation } = require('./hidemium.cjs');
const { exportCurrentVehicle } = require('./motorhomehire-profile-capture.cjs');

// Selectors verified against references/list of vehicles.mhtml.
const DEFAULT_LIST_URL = 'https://motorhomehire.com/wp-admin/edit.php?post_type=vehicle';

function validateListUrl(value) {
  const url = new URL(value);
  if (url.origin !== 'https://motorhomehire.com' || url.pathname !== '/wp-admin/edit.php' || url.searchParams.get('post_type') !== 'vehicle') throw new Error('Use the WordPress Vehicles list URL: https://motorhomehire.com/wp-admin/edit.php?post_type=vehicle');
  const page = Number(url.searchParams.get('paged') || 1);
  if (!Number.isSafeInteger(page) || page < 1) throw new Error('The list URL has an invalid page number.');
  return url.href;
}

function chooseListUrl(browser, requestedUrl) {
  if (requestedUrl) return validateListUrl(requestedUrl);
  const candidates = new Set();
  for (const context of browser.contexts()) {
    for (const page of context.pages()) {
      try { candidates.add(validateListUrl(page.url())); } catch { /* Other tabs are not vehicle lists. */ }
    }
  }
  if (candidates.size > 1) throw new Error('Multiple different vehicle list pages are open. Keep one list page open or specify --list-url "URL".');
  return [...candidates][0] || DEFAULT_LIST_URL;
}

// Read the visible list's rows once, before navigating to any vehicle editor.
function captureVehicleList() {
  const url = new URL(location.href);
  if (url.origin !== 'https://motorhomehire.com' || url.pathname !== '/wp-admin/edit.php' || url.searchParams.get('post_type') !== 'vehicle') throw new Error('The Vehicles list did not open. Check the WordPress login in Hidemium.');
  const table = document.querySelector('.wp-list-table');
  const body = table?.querySelector('#the-list');
  if (!body) throw new Error('The WordPress vehicle list table was not found.');
  const seen = new Set();
  const vehicles = [...body.querySelectorAll('tr[id^="post-"]')].map(row => {
    const postId = Number(/^post-(\d+)$/.exec(row.id)?.[1]);
    if (!row.classList.contains('type-vehicle') || !Number.isSafeInteger(postId) || postId <= 0 || seen.has(postId)) throw new Error(`Invalid or duplicate vehicle row: ${row.id}`);
    const link = row.querySelector('a.row-title');
    const editUrl = link && new URL(link.href, url);
    const checkbox = row.querySelector('input[name="post[]"]');
    if (!editUrl || editUrl.origin !== url.origin || editUrl.pathname !== '/wp-admin/post.php' || editUrl.searchParams.get('action') !== 'edit' || editUrl.searchParams.get('post') !== String(postId) || checkbox?.value !== String(postId)) throw new Error(`Vehicle ${postId}: list row and editor link identity disagree.`);
    const name = link.textContent.trim();
    if (!name) throw new Error(`Vehicle ${postId}: list title is blank.`);
    const publicLink = row.querySelector('a[rel="bookmark"]');
    const publicUrl = publicLink ? new URL(publicLink.href, url) : null;
    const countryFromUrl = publicUrl?.origin === url.origin && publicUrl.pathname.startsWith('/australia/') ? 'au' : publicUrl?.origin === url.origin && publicUrl.pathname.startsWith('/new-zealand/') ? 'nz' : undefined;
    const countries = ['au', 'nz'].filter(country => row.classList.contains(country === 'au' ? 'mh_location-australia' : 'mh_location-new-zealand'));
    seen.add(postId);
    return { postId, name, editUrl: editUrl.href, country: countryFromUrl || (countries.length === 1 ? countries[0] : undefined) };
  });
  if (!vehicles.length) throw new Error('No vehicles found on this list page. Check the list filters before downloading.');
  const pageNumber = Number(document.querySelector('#current-page-selector')?.value || url.searchParams.get('paged') || 1);
  const totalPages = Number(document.querySelector('.tablenav.top .total-pages')?.textContent.trim()) || null;
  const totalItemsText = document.querySelector('.tablenav.top .displaying-num')?.textContent || '';
  const totalItems = Number(totalItemsText.match(/[\d,]+/)?.[0].replace(/,/g, '')) || null;
  const next = document.querySelector('.tablenav.top a.next-page');
  const nextPageUrl = next && !next.classList.contains('disabled') && next.getAttribute('aria-disabled') !== 'true' ? next.href : null;
  return { listUrl: url.href, pageNumber, totalPages, totalItems, nextPageUrl, vehicles };
}

async function collectVehiclePages(page, selectedUrl, options, log) {
  const first = new URL(selectedUrl);
  if (!options.currentPage) first.searchParams.delete('paged');
  let listUrl = first.href, firstList;
  const vehicles = [], listPages = [], visited = new Set(), identities = new Set();
  while (listUrl) {
    validateListUrl(listUrl);
    if (visited.has(listUrl)) throw new Error('Repeated pagination URL; refusing to loop indefinitely.');
    visited.add(listUrl);
    log(`Reading Vehicles list: ${listUrl}`);
    await page.goto(listUrl, { waitUntil: 'load', timeout: 60_000 });
    validateListUrl(page.url());
    await page.locator('.wp-list-table #the-list').waitFor({ state: 'attached' });
    const list = await page.evaluate(captureVehicleList);
    const actual = new URL(list.listUrl);
    const expectedPage = Number(new URL(listUrl).searchParams.get('paged') || 1);
    if (list.pageNumber !== expectedPage) throw new Error(`Requested list page ${expectedPage}, but page ${list.pageNumber} opened. No JSONs were exported.`);
    for (const [key, value] of first.searchParams) {
      if (key !== 'paged' && actual.searchParams.get(key) !== value) throw new Error(`The list redirected and changed filter ${key}. No JSONs were exported.`);
    }
    firstList ||= list;
    if (list.totalPages !== firstList.totalPages || list.totalItems !== firstList.totalItems) throw new Error('Vehicle list totals changed during pagination. Rerun the download.');
    for (const vehicle of list.vehicles) {
      if (identities.has(vehicle.postId)) throw new Error(`Vehicle ${vehicle.postId} appeared on multiple pages. The list may have changed; rerun the download.`);
      identities.add(vehicle.postId);
      vehicles.push(vehicle);
    }
    listPages.push(list.listUrl);
    log(`Page ${list.pageNumber}${list.totalPages ? `/${list.totalPages}` : ''}: ${list.vehicles.length} vehicles; ${vehicles.length} collected.`);
    if (options.currentPage) break;
    if (!list.nextPageUrl) {
      if (list.totalPages && list.pageNumber < list.totalPages) throw new Error(`Next-page link missing on page ${list.pageNumber}/${list.totalPages}. No JSONs were exported.`);
      break;
    }
    const next = new URL(validateListUrl(list.nextPageUrl));
    if (Number(next.searchParams.get('paged') || 1) !== list.pageNumber + 1) throw new Error('Pagination did not advance to the next page. No JSONs were exported.');
    for (const [key, value] of first.searchParams) {
      if (key !== 'paged' && next.searchParams.get(key) !== value) throw new Error(`Next-page link changed filter ${key}. No JSONs were exported.`);
    }
    listUrl = next.href;
  }
  if (!options.currentPage && firstList.totalItems && vehicles.length !== firstList.totalItems) throw new Error(`Collected ${vehicles.length} vehicles, but the list reports ${firstList.totalItems}. No JSONs were exported.`);
  return { ...firstList, vehicles, listPages };
}

async function openVehicleEditor(page, vehicle) {
  await page.goto(vehicle.editUrl, { waitUntil: 'load', timeout: 60_000 });
  const url = new URL(page.url());
  if (url.origin !== 'https://motorhomehire.com' || url.pathname !== '/wp-admin/post.php' || url.searchParams.get('post') !== String(vehicle.postId)) throw new Error('Vehicle editor did not open. Check the WordPress login or vehicle access.');
  await page.locator('#title').waitFor({ state: 'visible' });
  if (await page.locator('[name="post_ID"]').inputValue() !== String(vehicle.postId) || await page.locator('[name="post_type"]').inputValue() !== 'vehicle') throw new Error('Unexpected vehicle identity in the editor; JSON was not exported.');
}

function readEditorCountry() {
  const permalink = document.querySelector('#sample-permalink a')?.href || document.querySelector('#view-post-btn a')?.href;
  if (!permalink) return null;
  const url = new URL(permalink, location.href);
  if (url.origin !== 'https://motorhomehire.com') return null;
  return url.pathname.startsWith('/australia/') ? 'au' : url.pathname.startsWith('/new-zealand/') ? 'nz' : null;
}

async function downloadCurrentPage(config, options = {}, dependencies = {}) {
  const connect = dependencies.connect || connectProfile;
  const prepare = dependencies.prepare || prepareAutomationTab;
  const exporter = dependencies.exportVehicle || exportCurrentVehicle;
  const cleanup = dependencies.cleanup || closeAutomation;
  const log = dependencies.log || console.log;
  if (options.listUrl) validateListUrl(options.listUrl);
  const browser = await connect({ profileId: config.hidemiumProfileId, apiUrl: config.hidemiumApiUrl });
  const pages = [], exported = [], failed = [];
  let runError;
  try {
    // Remember the open page/filter URL before replacing the profile's old tabs.
    const listUrl = chooseListUrl(browser, options.listUrl);
    const page = await prepare(browser, pages);
    const list = await collectVehiclePages(page, listUrl, options, log);
    log(`Found ${list.vehicles.length} vehicles across ${list.listPages.length} list page(s).`);
    if (options.listOnly) {
      for (const vehicle of list.vehicles) log(`Vehicle ${vehicle.postId}: ${vehicle.name}`);
      log('List check complete. No JSON files were written.');
      return { ...list, exported, failed };
    }
    for (const [index, vehicle] of list.vehicles.entries()) {
      log(`[${index + 1}/${list.vehicles.length}] Vehicle ${vehicle.postId}: ${vehicle.name}`);
      try {
        await openVehicleEditor(page, vehicle);
        let country = vehicle.country;
        const fallback = options.countryOverrides?.[vehicle.postId] || options.countryOverrides?.default;
        if (!country && fallback) country = await page.evaluate(readEditorCountry) || fallback;
        const snapshot = await exporter(page, { ...vehicle, country });
        exported.push({ postId: vehicle.postId, ...snapshot });
        log(`  Saved: captures/motorhomehire-profile/current/${snapshot.filename}`);
      } catch (error) {
        failed.push({ postId: vehicle.postId, reason: error.message });
        log(`  FAILED: ${error.message}`);
        if (page.isClosed() || !browser.isConnected()) break;
      }
    }
    log(`Download finished: ${exported.length}/${list.vehicles.length} vehicle JSONs saved to captures/motorhomehire-profile/current/.`);
    if (exported.length !== list.vehicles.length) throw new Error(`Download incomplete: ${exported.length} saved, ${failed.length} failed, ${list.vehicles.length - exported.length - failed.length} unprocessed. ${failed.map(item => `Vehicle ${item.postId}: ${item.reason}`).join(' | ')}`);
    return { ...list, exported, failed };
  } catch (error) { runError = error; throw error; }
  finally {
    try { await cleanup(browser, pages, config); }
    catch (error) { throw new Error(`${runError ? `${runError.message} | ` : ''}${error.message}`); }
  }
}

function parseArgs(args) {
  const options = {};
  for (let index = 0; index < args.length; index++) {
    if (args[index] === '--list-url') options.listUrl = validateListUrl(args[++index]);
    else if (args[index] === '--list-only') options.listOnly = true;
    else if (args[index] === '--current-page') options.currentPage = true;
    else if (args[index] === '--help') options.help = true;
    else throw new Error(`Unknown option: ${args[index]}. Use --help.`);
  }
  return options;
}

async function main(args = process.argv.slice(2)) {
  const options = parseArgs(args);
  if (options.help) {
    console.log('node scripts/download-motorhomehire-profiles.cjs [--list-url "VEHICLES_LIST_URL"] [--current-page] [--list-only]');
    console.log('Downloads all pages from page 1, preserving the selected list filters. Use --current-page for one page only. JSONs go to captures/motorhomehire-profile/current/.');
    return;
  }
  const config = readSiteConfig('motorhomehireProfile');
  const countryFile = path.join(root, 'config', 'download-country-overrides.json');
  options.countryOverrides = fs.existsSync(countryFile) ? JSON.parse(fs.readFileSync(countryFile, 'utf8').replace(/^\uFEFF/, '')) : {};
  if (!options.countryOverrides || typeof options.countryOverrides !== 'object' || Array.isArray(options.countryOverrides) || Object.entries(options.countryOverrides).some(([id, country]) => (id !== 'default' && !/^[1-9]\d*$/.test(id)) || !['au', 'nz'].includes(country))) throw new Error('download-country-overrides.json must map vehicle IDs (or default) to au or nz.');
  return downloadCurrentPage(config, options);
}

if (require.main === module) main().catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { DEFAULT_LIST_URL, validateListUrl, chooseListUrl, captureVehicleList, collectVehiclePages, openVehicleEditor, downloadCurrentPage, parseArgs, main };
