const { SITE_URL, countrySite } = require('./campervanfinder-schema.cjs');
const { captureFolder, readSiteConfig } = require('./project.cjs');
const { captureProfile, writeCurrent } = require('./campervanfinder-profile.cjs');
const { targetEditor } = require('./campervanfinder-editor.cjs');
const { connectProfile, prepareAutomationTab, closeAutomation } = require('./hidemium.cjs');
const { loadCampervanfinderPage } = require('./campervanfinder-loading.cjs');

const DEFAULT_LIST_URL = `${SITE_URL}/wp-admin/edit.php?post_type=product`;

function validateListUrl(value, country = 'au') {
  const siteUrl = countrySite(country).origin;
  const url = new URL(value);
  if (url.origin !== siteUrl || url.pathname !== '/wp-admin/edit.php' || url.searchParams.getAll('post_type').length !== 1 || url.searchParams.get('post_type') !== 'product') throw new Error(`Use the Campervan Finder Products list: ${siteUrl}/wp-admin/edit.php?post_type=product`);
  const page = url.searchParams.getAll('paged');
  if (page.length > 1 || page.length && (!/^[1-9]\d*$/.test(page[0]) || !Number.isSafeInteger(Number(page[0])))) throw new Error('Invalid Products list page number.');
  url.hash = '';
  return url.href;
}

function chooseListUrl(browser, requestedUrl, country = 'au') {
  if (requestedUrl) return validateListUrl(requestedUrl, country);
  const candidates = new Set();
  for (const context of browser.contexts()) {
    for (const page of context.pages()) {
      try { candidates.add(validateListUrl(page.url(), country)); } catch { /* Other sites/editors are not list pages. */ }
    }
  }
  if (candidates.size > 1) throw new Error('Multiple different Campervan Finder list pages are open. Keep one list page open or specify --list-url "URL".');
  return [...candidates][0] || `${countrySite(country).origin}/wp-admin/edit.php?post_type=product`;
}

// Selectors checked against references/campervanfinder/list of products.mhtml.
// This runs only in the list page and reads rows/pagination without changing forms.
function captureProductList({ siteUrl }) {
  const url = new URL(location.href);
  if (url.origin !== siteUrl || url.pathname !== '/wp-admin/edit.php' || url.searchParams.get('post_type') !== 'product') throw new Error('The Campervan Finder Products list did not open. Check the WordPress login.');
  const body = document.querySelector('.wp-list-table #the-list');
  if (!body) throw new Error('The WordPress product list table was not found.');
  const seen = new Set();
  const vehicles = [...body.querySelectorAll('tr[id^="post-"]')].map(row => {
    const postId = Number(/^post-([1-9]\d*)$/.exec(row.id)?.[1]);
    if (!row.classList.contains('type-product') || !Number.isSafeInteger(postId) || seen.has(postId)) throw new Error(`Invalid or duplicate product row: ${row.id}`);
    const link = row.querySelector('a.row-title');
    const editUrl = link && new URL(link.href, url);
    const checkbox = row.querySelector('input[name="post[]"]');
    if (!editUrl || editUrl.origin !== siteUrl || editUrl.pathname !== '/wp-admin/post.php' || editUrl.searchParams.get('post') !== String(postId) || editUrl.searchParams.get('action') !== 'edit' || checkbox?.value !== String(postId)) throw new Error(`Product ${postId}: row, checkbox, and editor link identities disagree.`);
    const name = link.textContent.trim();
    if (!name) throw new Error(`Product ${postId}: list title is blank.`);
    seen.add(postId);
    return { postId, name, editUrl: editUrl.href };
  });
  if (!vehicles.length) throw new Error('No vehicles found on this Products list page. Check the selected filters.');
  const next = document.querySelector('.tablenav.top a.next-page');
  const nextPageUrl = next && !next.classList.contains('disabled') && next.getAttribute('aria-disabled') !== 'true' ? next.href : null;
  const pageText = document.querySelector('#current-page-selector')?.value || url.searchParams.get('paged') || '1';
  const pagesText = document.querySelector('.tablenav.top .total-pages')?.textContent.trim();
  const itemsText = document.querySelector('.tablenav.top .displaying-num')?.textContent.match(/[\d,]+/)?.[0].replace(/,/g, '');
  const pageNumber = Number(pageText), totalPages = Number(pagesText || (!nextPageUrl ? '1' : ''));
  const totalItems = Number(itemsText);
  if (!/^[1-9]\d*$/.test(pageText) || !Number.isSafeInteger(pageNumber) || !Number.isSafeInteger(totalPages) || totalPages < pageNumber || !Number.isSafeInteger(totalItems) || totalItems < vehicles.length) throw new Error('Invalid or missing product-list pagination totals.');
  return { listUrl: url.href, pageNumber, totalPages, totalItems, nextPageUrl, vehicles };
}

function filters(url) {
  return JSON.stringify([...new URL(url).searchParams].filter(([key]) => key !== 'paged').sort((a, b) => a[0].localeCompare(b[0]) || a[1].localeCompare(b[1])));
}

function checkListPage(list, requestedUrl, firstList, seenIds, currentPage, country = 'au') {
  validateListUrl(list.listUrl, country);
  const expectedPage = Number(new URL(requestedUrl).searchParams.get('paged') || 1);
  if (list.pageNumber !== expectedPage || Number(new URL(list.listUrl).searchParams.get('paged') || 1) !== expectedPage) throw new Error(`Requested list page ${expectedPage}, but a different page opened.`);
  if (filters(list.listUrl) !== filters(requestedUrl)) throw new Error('The product list redirected and changed the selected filters.');
  if (firstList && (list.totalPages !== firstList.totalPages || list.totalItems !== firstList.totalItems)) throw new Error('Product list totals changed during the download. Rerun to capture a consistent selection.');
  const pageIds = new Set();
  for (const vehicle of list.vehicles) {
    if (seenIds.has(vehicle.postId) || pageIds.has(vehicle.postId)) throw new Error(`Product ${vehicle.postId} appeared more than once across list pages. Rerun the download.`);
    pageIds.add(vehicle.postId);
  }
  // Validate Next before exporting this page, while its pagination is still available.
  if (!currentPage) {
    if (!list.nextPageUrl && list.pageNumber !== list.totalPages) throw new Error(`Next-page link missing on page ${list.pageNumber}/${list.totalPages}.`);
    if (list.nextPageUrl) {
      const next = new URL(validateListUrl(list.nextPageUrl, country));
      if (list.pageNumber >= list.totalPages || Number(next.searchParams.get('paged') || 1) !== list.pageNumber + 1) throw new Error('Pagination did not advance to the next page.');
      if (filters(next.href) !== filters(requestedUrl)) throw new Error('Next-page link changed the selected filters.');
    }
  }
  for (const postId of pageIds) seenIds.add(postId);
}

async function downloadProfiles(config, options = {}, dependencies = {}) {
  const country = options.country || 'au';
  const site = countrySite(country);
  const connect = dependencies.connect || connectProfile;
  const prepare = dependencies.prepare || prepareAutomationTab;
  const capture = dependencies.capture || captureProfile;
  const store = dependencies.store || writeCurrent;
  const openEditor = dependencies.openEditor || targetEditor;
  const loadPage = dependencies.loadPage || loadCampervanfinderPage;
  const cleanup = dependencies.cleanup || closeAutomation;
  const folder = dependencies.workspace || captureFolder(country === 'nz' ? 'campervanfinderNzProfile' : 'campervanfinderAuProfile');
  const log = dependencies.log || (message => console.log(`[${new Date().toISOString()}] ${message}`));
  if (options.listUrl) validateListUrl(options.listUrl, country);
  const browser = await connect({ profileId: config.hidemiumProfileId, apiUrl: config.hidemiumApiUrl });
  const pages = [], listPages = [], vehicles = [], exported = [], failed = [];
  const visited = new Set(), seenIds = new Set();
  let firstList, runError;
  try {
    const start = new URL(chooseListUrl(browser, options.listUrl, country));
    if (!options.currentPage) start.searchParams.delete('paged');
    const page = await prepare(browser, pages);
    let listUrl = start.href;
    while (listUrl) {
      validateListUrl(listUrl, country);
      if (visited.has(listUrl)) throw new Error('Repeated product-list pagination URL; refusing to loop indefinitely.');
      visited.add(listUrl);
      log(`Reading Products list: ${listUrl}`);
      await loadPage(page, listUrl, { country, kind: 'list', timeout: config.pageTimeoutMs, log });
      validateListUrl(page.url(), country);
      await page.locator('.wp-list-table #the-list').waitFor({ state: 'attached' });
      const list = await page.evaluate(captureProductList, { siteUrl: site.origin });
      checkListPage(list, listUrl, firstList, seenIds, options.currentPage, country);
      firstList ||= list;
      listPages.push(list.listUrl);
      vehicles.push(...list.vehicles);
      log(`Page ${list.pageNumber}/${list.totalPages}: ${list.vehicles.length} vehicles; ${list.totalItems} in the selected list.`);
      for (const vehicle of list.vehicles) {
        if (options.listOnly) { log(`Product ${vehicle.postId}: ${vehicle.name}`); continue; }
        log(`[${exported.length + failed.length + 1}/${options.currentPage ? list.vehicles.length : firstList.totalItems}] Product ${vehicle.postId}: ${vehicle.name}`);
        try {
          await openEditor(page, vehicle.postId, vehicle.editUrl, { country, timeout: config.pageTimeoutMs, log });
          const snapshot = await capture(page, vehicle.postId, { country });
          if (snapshot.capture?.postId !== vehicle.postId) throw new Error('Captured JSON has the wrong product identity.');
          const saved = await store(snapshot, folder);
          exported.push({ postId: vehicle.postId, filename: saved.filename });
          for (const warning of snapshot.audit?.warnings || []) log(`WARNING product ${vehicle.postId}: ${warning}`);
          log(`Saved: ${site.folder}/current/${saved.filename}`);
        } catch (error) {
          failed.push({ postId: vehicle.postId, reason: error.message });
          log(`FAILED product ${vehicle.postId}: ${error.message}`);
          if (page.isClosed() || !browser.isConnected()) throw new Error(`Browser disconnected or automation tab closed while downloading product ${vehicle.postId}.`);
        }
      }
      // Capture Next before opening editors; finish this page's exports first.
      listUrl = options.currentPage ? null : list.nextPageUrl;
    }
    if (!options.currentPage && vehicles.length !== firstList.totalItems) throw new Error(`Read ${vehicles.length} unique products, but the list reports ${firstList.totalItems}. Download incomplete.`);
    if (options.listOnly) log(`List check complete: ${vehicles.length} vehicles across ${listPages.length} pages. No JSONs were written.`);
    else {
      log(`Download finished: ${exported.length}/${vehicles.length} JSONs saved across ${listPages.length} pages; ${failed.length} failed.`);
      if (failed.length) throw new Error(`Download incomplete: ${exported.length} saved, ${failed.length} failed. ${failed.map(item => `Product ${item.postId}: ${item.reason}`).join(' | ')}`);
    }
    return { listUrl: firstList.listUrl, totalPages: firstList.totalPages, totalItems: firstList.totalItems, listPages, vehicles, exported, failed };
  } catch (error) {
    runError = error;
    log(`Download stopped: ${error.message}. ${exported.length} JSONs saved, ${failed.length} product exports failed, ${listPages.length} list pages read. Existing completed exports are retained.`);
    throw error;
  } finally {
    try { await cleanup(browser, pages, config); }
    catch (error) { throw new Error(`${runError ? `${runError.message} | ` : ''}${error.message}`); }
  }
}

function parseArgs(args) {
  const options = {};
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--list-url') {
      if (!args[i + 1] || args[i + 1].startsWith('--')) throw new Error('--list-url requires a Products list URL.');
      options.listUrl = args[++i];
    } else if (args[i] === '--country') {
      if (!args[i + 1] || args[i + 1].startsWith('--')) throw new Error('--country requires au or nz.');
      options.country = args[++i];
    } else if (args[i] === '--list-only') options.listOnly = true;
    else if (args[i] === '--current-page') options.currentPage = true;
    else if (args[i] === '--help') options.help = true;
    else throw new Error(`Unknown option: ${args[i]}. Use --help.`);
  }
  countrySite(options.country || 'au');
  if (options.listUrl) options.listUrl = validateListUrl(options.listUrl, options.country || 'au');
  return options;
}

async function main(args = process.argv.slice(2)) {
  const options = parseArgs(args);
  if (options.help) {
    console.log('node scripts/download-campervanfinder-profiles.cjs [--country au|nz] [--list-url "PRODUCTS_LIST_URL"] [--current-page] [--list-only]');
    console.log('Starts at page 1 with the selected filters. AU JSONs go to captures/campervanfinder-au/current/; NZ to captures/campervanfinder-nz/current/.');
    return;
  }
  const siteId = options.country === 'nz' ? 'campervanfinderNzProfile' : 'campervanfinderAuProfile';
  return downloadProfiles(readSiteConfig(siteId), options);
}

if (require.main === module) main().catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { DEFAULT_LIST_URL, validateListUrl, chooseListUrl, captureProductList, checkListPage, downloadProfiles, parseArgs, main };
