const fs = require('node:fs');
const path = require('node:path');
const { randomBytes } = require('node:crypto');
const { SITE, validate } = require('./motorhomehire-search-schema.cjs');
const { captureFolder, readSiteConfig, readJson, atomicWriteText: atomicWrite } = require('./project.cjs');
const workspace = captureFolder('motorhomehireAdmin');
const { targetEditor, readForm, snapshot } = require('./motorhomehire-search-editor.cjs');
const { connectProfile, prepareAutomationTab, closeAutomation } = require('./hidemium.cjs');
const LIST_URL = `${SITE}/vehicles`;
const log = message => console.log(`[${new Date().toISOString()}] ${message}`);

function captureList(includeCreationIdentity = false) {
  if (location.href !== 'https://admin.motorhomehire.com/vehicles') throw new Error('Backend vehicle list did not open. Check the backend login.');
  if (!window.jQuery?.fn?.dataTable?.isDataTable('#vehicle_table')) throw new Error('Backend DataTable is unavailable.');
  const api = window.jQuery('#vehicle_table').DataTable(), info = api.page.info();
  const data = api.rows({ page: 'current' }).data().toArray();
  const rows = api.rows({ page: 'current' }).nodes().toArray();
  if (data.length !== rows.length || info.end - info.start !== rows.length || !rows.length) throw new Error('Backend list rows are incomplete or empty.');
  if (!info.serverSide || !Number.isSafeInteger(info.recordsTotal) || info.recordsTotal !== info.recordsDisplay || info.recordsTotal < rows.length || info.pages !== Math.ceil(info.recordsTotal / info.length)) throw new Error('Invalid or filtered backend list totals.');
  const filters = { key: document.querySelector('.key')?.value, company: document.querySelector('#company_search')?.value, type: document.querySelector('#vehicle_type_search')?.value };
  if (filters.key !== '' || filters.company !== '0' || filters.type !== '0') throw new Error('Backend list filters changed; refusing an incomplete full download.');
  const seen = new Set();
  const vehicles = data.map((item,i) => {
    const id = String(item.id), row = rows[i];
    if (!/^[1-9]\d*$/.test(id) || !Number.isSafeInteger(Number(id)) || seen.has(id)) throw new Error('Invalid or duplicate backend list ID.');
    const links = row.querySelectorAll('a.btn-edit-vehicle');
    if (links.length !== 1 || links[0].href !== `https://admin.motorhomehire.com/vehicles/detail/${id}` || row.querySelector('.switch-status')?.dataset.id !== id) throw new Error(`Backend ${id}: row/editor identities disagree.`);
    const name = row.cells[2]?.textContent.trim(), company = row.cells[4]?.textContent.trim();
    if (!name) throw new Error(`Backend ${id}: missing vehicle name.`);
    seen.add(id);
    // The live list now reports rental_company_id 0 for every row; creation then matches the company name.
    const creationIdentity = includeCreationIdentity ? { providerId: String(item.vehicle_id_provider ?? ''), companyId: String(item.rental_company_id ?? '') } : {};
    if (includeCreationIdentity && (!Object.hasOwn(item, 'vehicle_id_provider') || !/^\d+$/.test(creationIdentity.companyId))) throw new Error(`Backend ${id}: creation duplicate-check identity is missing.`);
    return { id, name, company, editUrl: links[0].href, ...creationIdentity };
  });
  return { page: info.page + 1, pages: info.pages, total: info.recordsTotal, start: info.start, end: info.end, length: info.length, vehicles };
}
async function waitForList(page, number, timeout) {
  await page.waitForFunction(expected => {
    if (!window.jQuery?.fn?.dataTable?.isDataTable('#vehicle_table')) return false;
    const api = window.jQuery('#vehicle_table').DataTable(), settings = api.settings()[0];
    return api.page.info().page === expected - 1 && !settings.bDrawing && Number(api.ajax.json()?.draw) === settings.iDraw && api.rows({ page: 'current' }).data().length > 0;
  }, number, { timeout });
}
async function collectVehicles(page, timeout, progress = log, includeCreationIdentity = false) {
  await page.goto(LIST_URL, { waitUntil: 'domcontentloaded', timeout });
  if (page.url() !== LIST_URL) throw new Error('Backend list did not open. Log in to admin.motorhomehire.com in the configured Hidemium profile.');
  await page.waitForFunction(() => window.jQuery?.fn?.dataTable?.isDataTable('#vehicle_table') && !window.jQuery('#vehicle_table').DataTable().settings()[0].bDrawing, null, { timeout });
  // stateSave may restore a later page. Clear filters and explicitly draw page 1.
  await page.evaluate(() => {
    document.querySelector('.key').value = '';
    document.querySelector('#company_search').value = '0';
    document.querySelector('#vehicle_type_search').value = '0';
    window.jQuery('#vehicle_table').DataTable().page.len(10).page(0).draw('page');
  });
  const vehicles = [], ids = new Set(); let first;
  for (let number = 1; ; number++) {
    await waitForList(page, number, timeout);
    const list = await page.evaluate(captureList, includeCreationIdentity);
    first ||= list;
    if (list.page !== number || list.pages !== first.pages || list.total !== first.total || list.length !== first.length || list.start !== vehicles.length) throw new Error('Backend pagination or totals changed during collection. Rerun the download.');
    for (const vehicle of list.vehicles) {
      if (ids.has(vehicle.id)) throw new Error(`Backend ${vehicle.id} appeared on multiple list pages.`);
      ids.add(vehicle.id); vehicles.push(vehicle);
    }
    progress(`List page ${number}/${list.pages}: ${list.vehicles.length} vehicles; ${vehicles.length}/${list.total} collected.`);
    if (number === list.pages) break;
    await page.evaluate(() => window.jQuery('#vehicle_table').DataTable().page('next').draw('page'));
  }
  if (vehicles.length !== first.total) throw new Error(`Collected ${vehicles.length} vehicles, but the backend reports ${first.total}.`);
  return { total: first.total, pages: first.pages, vehicles };
}
function profileFilename(d) {
  const clean = value => String(value).replace(/[<>:"/\\|?*\x00-\x1f]/g, ' ').replace(/\s+/g, ' ').trim().replace(/[. ]+$/g,'');
  const title = clean(`${d.fields.rental_company.text} - ${d.fields.name_vehicle}`).slice(0,170).replace(/[. ]+$/g,'');
  if (!title) throw new Error('Cannot name a backend profile without its company/name.');
  return `${d.source.vehicle_id} - ${title}.json`;
}
function inside(folder, candidate) {
  const root = path.resolve(folder), resolved = path.resolve(candidate);
  if (!resolved.startsWith(root + path.sep)) throw new Error('Download paths must remain inside the backend workspace.');
  return resolved;
}
function installFreshDatabase(stage, folder = workspace) {
  const current = inside(folder, path.join(folder,'current'));
  const staged = inside(folder, path.join(stage,'current'));
  const old = inside(folder, path.join(stage,'previous-current'));
  if (!fs.lstatSync(staged).isDirectory() || fs.lstatSync(staged).isSymbolicLink()) throw new Error('Staged download is not a regular directory.');
  let moved = false;
  if (fs.existsSync(current)) {
    if (!fs.lstatSync(current).isDirectory() || fs.lstatSync(current).isSymbolicLink()) throw new Error('Current backend database is not a regular directory.');
    fs.renameSync(current, old); moved = true;
  }
  try { fs.renameSync(staged, current); }
  catch (error) {
    if (moved) fs.renameSync(old, current);
    throw error;
  }
  return { current, previous: moved ? old : null };
}
async function downloadProfiles(config, options = {}) {
  const timeout = config.pageTimeoutMs || 60000;
  const runId = `DOWNLOAD-${new Date().toISOString().replace(/[-:TZ.]/g,'')}-${randomBytes(3).toString('hex')}`;
  const stage = inside(workspace, path.join(workspace,'downloads',runId));
  const staged = path.join(stage,'current');
  const browser = await connectProfile({ profileId: config.hidemiumProfileId, apiUrl: config.hidemiumApiUrl });
  const pages = [], exported = [], failed = []; let list, failure;
  try {
    const page = await prepareAutomationTab(browser,pages); page.setDefaultTimeout(timeout);
    list = await collectVehicles(page,timeout);
    if (options.listOnly) {
      for (const v of list.vehicles) log(`Backend ${v.id}: ${v.company} - ${v.name}`);
      log(`List check complete: ${list.total} vehicles across ${list.pages} pages. No JSON files written.`);
    } else {
      fs.mkdirSync(staged,{recursive:true});
      for (const [index,vehicle] of list.vehicles.entries()) {
        log(`[${index+1}/${list.total}] Backend ${vehicle.id}: ${vehicle.company} - ${vehicle.name}`);
        let error;
        // Retry reads only. No website form submission is performed.
        for (let attempt=1; attempt<=2; attempt++) {
          try {
            await targetEditor(page,vehicle.id,timeout);
            const d = snapshot(await readForm(page),{ source: { vehicle_id: vehicle.id } });
            if (d.fields.name_vehicle !== vehicle.name || (vehicle.company && d.fields.rental_company.text !== vehicle.company)) throw new Error('Live editor name/company changed since list collection.');
            if (!vehicle.company) log(`Backend ${vehicle.id}: company is blank in the list; captured the live editor selection (${d.fields.rental_company.text}).`);
            const filename = profileFilename(d);
            atomicWrite(path.join(staged,filename),JSON.stringify(d,null,2)+'\n');
            exported.push({id:vehicle.id,filename}); error=null; break;
          } catch(e) { error=e; log(`Backend ${vehicle.id} read attempt ${attempt}: ${e.message}`); if (page.isClosed() || !browser.isConnected()) break; }
        }
        if(error)failed.push({id:vehicle.id,reason:error.message});
        if(page.isClosed() || !browser.isConnected()) throw new Error('Browser disconnected during the backend download.');
      }
      if(failed.length || exported.length!==list.total) throw new Error(`Incomplete backend download: ${exported.length}/${list.total} exported; ${failed.length} failed. Existing current database retained.`);
      // Re-read every staged JSON before replacing the old database.
      const seen = new Set();
      for(const item of exported) {
        const d=validate(readJson(path.join(staged,item.filename)));
        if(d.source.vehicle_id!==item.id || seen.has(item.id))throw new Error('Staged backend database has invalid/duplicate identities.');
        seen.add(item.id);
      }
      // Confirm the complete ID set did not change while profiles were being read.
      const finalList=await collectVehicles(page,timeout);
      if(finalList.total!==list.total || finalList.vehicles.some(v=>!seen.has(v.id)))throw new Error('Backend vehicle list changed during download. Existing current database retained.');
    }
  } catch(e) { failure=e; }
  finally {
    log('Closing the automation tab and Hidemium browser profile.');
    try { await closeAutomation(browser,pages,config); }
    catch(e) { failure=new Error(`${failure?failure.message+' | ':''}${e.message}`); }
  }
  if(!options.listOnly && fs.existsSync(stage)) {
    const manifest={runId,exportedAt:new Date().toISOString(),source:LIST_URL,total:list?.total,pages:list?.pages,exported,failed,status:failure?'failed':'downloaded',reason:failure?.message};
    if(!failure) {
      try { const installed=installFreshDatabase(stage); manifest.status='done'; manifest.previousDatabase=installed.previous; log(`Fresh backend database installed: ${exported.length} profiles in ${installed.current}`); }
      catch(e) { failure=e; manifest.status='failed'; manifest.reason=e.message; }
    }
    atomicWrite(path.join(stage,'download-result.json'),JSON.stringify(manifest,null,2)+'\n');
  }
  if(failure)throw failure;
  return { total:list.total,pages:list.pages,exported,failed };
}
async function main(args=process.argv.slice(2)) {
  if(args.some(a=>!['--list-only','--help'].includes(a)))throw new Error('Usage: node scripts/download-motorhomehire-search.cjs [--list-only]');
  if(args.includes('--help')) { console.log('Downloads every backend vehicle from all unfiltered list pages, starting at page 1. Replaces captures/motorhomehire-search/current only after a complete successful export. --list-only reads the list without exporting.'); return; }
  return downloadProfiles(readSiteConfig('motorhomehireAdmin'),{listOnly:args.includes('--list-only')});
}
if(require.main===module)main().catch(e=>{console.error(e.message);process.exitCode=1;});
module.exports={captureList,waitForList,collectVehicles,profileFilename,installFreshDatabase,downloadProfiles,main};
