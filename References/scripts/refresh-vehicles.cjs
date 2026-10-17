// Downloads the live profiles of chosen catalog vehicles (read-only) into
// captures/<site>/current and refreshes their combined catalog records.
const fs = require('node:fs');
const path = require('node:path');
const store = require('./catalog-store.cjs');
const fields = require('./catalog-fields.cjs');
const adapters = require('./catalog-adapters.cjs');
const { loadSettings } = require('./catalog-plan.cjs');
const runner = require('./catalog-runner.cjs');
const { storeCapture } = require('./capture-profile.cjs');
const project = require('./project.cjs');

const JOB = 'jobs/refresh-vehicles.json';
const usage = `Download the current website profiles of saved vehicles without editing the websites:
  node scripts/refresh-vehicles.cjs --vehicles veh_000196,veh_000197
  node scripts/refresh-vehicles.cjs --brand "TREK Campers" [--country au]
  node scripts/refresh-vehicles.cjs --all
  node scripts/refresh-vehicles.cjs                 (reads ${JOB})
Add --sites ${Object.keys(fields.SITES).join(',')} to limit the websites.`;

function parseArgs(args) {
  const options = {};
  for (let i = 0; i < args.length; i++) {
    const argument = args[i];
    if (['--help', '--all'].includes(argument)) options[argument.slice(2)] = true;
    else if (['--vehicles', '--brand', '--country', '--sites', '--job'].includes(argument)) {
      const key = argument.slice(2);
      if (options[key] || !args[i + 1] || args[i + 1].startsWith('--')) throw new Error(`Missing or duplicate value for ${argument}.`);
      options[key] = args[++i];
    } else throw new Error(`Unknown option: ${argument}.`);
  }
  if (options.help) return options;
  const list = value => value?.split(',').map(item => item.trim()).filter(Boolean);
  const selectors = ['all', 'vehicles', 'brand'].filter(key => options[key]);
  if (selectors.length > 1) throw new Error('Choose one of --all, --vehicles or --brand.');
  if (options.country && !['au', 'nz'].includes(options.country)) throw new Error('--country must be au or nz.');
  return { all: !!options.all, vehicles: list(options.vehicles), brand: options.brand, country: options.country, sites: list(options.sites), job: options.job };
}
// A job file holds {"vehicles": ["veh_000196", ...] | "all", "brand"?, "country"?, "sites"?}.
function readJob(root, file = JOB) {
  const job = store.readJson(store.inside(root, file));
  if (job.exampleOnly) throw new Error(`${file} is an example. Copy it, choose the vehicles and remove exampleOnly.`);
  const vehicles = job.vehicles === 'all' ? undefined : job.vehicles;
  if (job.vehicles !== undefined && job.vehicles !== 'all' && (!Array.isArray(vehicles) || !vehicles.length)) throw new Error('Job "vehicles" must be "all" or a nonempty array of vehicle IDs.');
  if (job.vehicles === undefined && !job.brand) throw new Error('Job needs "vehicles" or "brand".');
  return { all: job.vehicles === 'all', vehicles, brand: job.brand, country: job.country, sites: job.sites };
}
function selectRecords(current, selection) {
  const records = [...current.values()].map(entry => entry.record);
  if (selection.vehicles) {
    const missing = selection.vehicles.filter(id => !current.has(id));
    if (missing.length) throw new Error(`Not in the catalog: ${missing.join(', ')}.`);
    return selection.vehicles.map(id => current.get(id).record);
  }
  const brand = selection.brand && schemaKey(selection.brand);
  const chosen = records.filter(record => (!brand || [record.identity.brand, record.identity.supplierKey].some(value => schemaKey(value || '') === brand))
    && (!selection.country || record.identity.country === selection.country));
  if (!selection.all && !brand) throw new Error('Choose vehicles: --vehicles, --brand, --all or a job file.');
  if (!chosen.length) throw new Error('No catalog vehicle matches that selection.');
  return chosen.sort((a, b) => a.vehicleId.localeCompare(b.vehicleId));
}
const schemaKey = value => String(value).toLowerCase().replace(/[^a-z0-9]/g, '');
// Every confirmed website link of the chosen vehicles on enabled, supported sites.
function targetsFor(records, settings, sites) {
  const targets = [], skipped = [];
  for (const record of records) for (const [siteId, site] of Object.entries(record.sites)) for (const [externalId, external] of Object.entries(site.records)) {
    const label = `${record.vehicleId} ${siteId}:${externalId}`;
    if (sites && !sites.includes(siteId)) continue;
    if (!fields.SITES[siteId]) { skipped.push(`${label} (no downloader for this website yet)`); continue; }
    if (!settings.sites.sites[siteId]?.enabled) { skipped.push(`${label} (website disabled in catalog/config/sites.json)`); continue; }
    if (external.mapping.status !== 'confirmed') { skipped.push(`${label} (link not confirmed)`); continue; }
    const baseline = external.observed?.snapshot || { capture: { editUrl: external.mapping.editUrl }, source: { vehicle_id: externalId, url: external.mapping.editUrl } };
    targets.push({ vehicleId: record.vehicleId, country: record.identity.country, siteId, externalId, mapping: store.clone(external.mapping), baseline });
  }
  // One browser profile at a time.
  const order = Object.keys(fields.SITES);
  targets.sort((a, b) => order.indexOf(a.siteId) - order.indexOf(b.siteId) || a.vehicleId.localeCompare(b.vehicleId));
  return { targets, skipped };
}
async function refresh(root, selection, { capture = adapters.capture, sessionFactory = runner.sessions, log = console.log, ...sessionOptions } = {}) {
  const settings = loadSettings(root);
  if (selection.sites) for (const siteId of selection.sites) if (!fields.SITES[siteId]) throw new Error(`Unknown website: ${siteId}. Use ${Object.keys(fields.SITES).join(', ')}.`);
  const records = selectRecords(store.scanCurrent(root), selection);
  const { targets, skipped } = targetsFor(records, settings, selection.sites);
  const runId = `REFRESH-${new Date().toISOString().replace(/\D/g, '').slice(0, 17)}`;
  const folder = store.inside(root, `changes/${runId}`);
  const result = { schemaVersion: '1.0', runId, startedAt: new Date().toISOString(), selection, vehicles: records.map(r => r.vehicleId), skipped, targets: [] };
  const persist = () => { fs.mkdirSync(folder, { recursive: true }); store.atomicWrite(path.join(folder, 'results.json'), result); fs.writeFileSync(path.join(folder, 'results.txt'), summary(result)); };
  log(`${records.length} vehicle(s), ${targets.length} website profile(s) to download.`);
  for (const line of skipped) log(`Skipped ${line}.`);
  if (!targets.length) { result.status = 'nothing'; persist(); return result; }
  const configurations = Object.fromEntries([...new Set(targets.map(t => t.siteId))].map(siteId => [siteId, runner.connection(root, siteId, settings)]));
  persist();
  await runner.withProfileLocks(root, configurations, async () => {
    const browser = sessionFactory(sessionOptions);
    try {
      for (const target of targets) {
        const outcome = { vehicleId: target.vehicleId, siteId: target.siteId, externalId: target.externalId };
        result.targets.push(outcome);
        try {
          const config = configurations[target.siteId];
          const page = await browser.page(config);
          const snapshot = await capture(page, target, { timeout: config.pageTimeoutMs, log });
          const saved = storeCapture(target.siteId, snapshot, { externalId: target.externalId, country: target.country, root });
          const merged = store.incorporate(root, target, snapshot, { phase: 'refresh', reconcile: record => fields.reconcile(record, settings.policies) });
          Object.assign(outcome, { status: 'downloaded', file: saved.file, catalogRevision: merged.record.catalogRevision });
          log(`${target.vehicleId}: downloaded ${fields.SITES[target.siteId].label} ${target.externalId} -> ${saved.file}`);
        } catch (error) {
          Object.assign(outcome, { status: 'failed', error: error.message });
          log(`${target.vehicleId}: could not download ${fields.SITES[target.siteId].label} ${target.externalId}: ${error.message}`);
          try { store.recordReadFailure(root, target, error); } catch {}
        }
        persist();
      }
    } finally { await browser.close(); }
  });
  result.finishedAt = new Date().toISOString();
  result.status = result.targets.every(t => t.status === 'downloaded') ? 'done' : 'partial';
  persist();
  log(`done: changes/${runId}/results.txt`);
  return result;
}
function summary(result) {
  const lines = [`Run: ${result.runId}`, `Status: ${result.status || 'running'}`, `Vehicles: ${result.vehicles.join(', ')}`, ''];
  for (const t of result.targets) lines.push(`${t.status === 'downloaded' ? 'OK    ' : 'FAILED'} ${t.vehicleId} ${t.siteId}:${t.externalId}${t.file ? ` -> ${t.file}` : ''}${t.error ? ` (${t.error})` : ''}`);
  if (result.skipped.length) lines.push('', 'Skipped:', ...result.skipped.map(s => `  ${s}`));
  return lines.join('\n') + '\n';
}
async function main(args = process.argv.slice(2)) {
  const options = parseArgs(args);
  if (options.help) { console.log(usage); return; }
  const selection = options.all || options.vehicles || options.brand ? options : { ...readJob(project.root, options.job), ...(options.sites ? { sites: options.sites } : {}) };
  const result = await refresh(project.root, selection);
  if (result.status === 'partial') process.exitCode = 1;
}
if (require.main === module) main().catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { parseArgs, readJob, selectRecords, targetsFor, refresh };
