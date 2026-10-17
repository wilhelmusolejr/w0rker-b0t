// Reviewed creation of a new Motorhome Hire public vehicle profile from a
// ready frontend draft. Saves once as a WordPress Draft; never publishes.
const fs = require('node:fs');
const path = require('node:path');
const { randomUUID } = require('node:crypto');
const store = require('./catalog-store.cjs');
const fields = require('./catalog-fields.cjs');
const schema = require('./motorhomehire-frontend-schema.cjs');
const drafts = require('./motorhomehire-frontend-draft.cjs');
const editor = require('./motorhomehire-frontend-editor.cjs');
const mhEditor = require('./motorhomehire-profile-editor.cjs');
const { fieldMappings, specialFeatureLabels } = require('./motorhomehire-profile-schema.cjs');
const { captureVehicleProfile, storeCurrentSnapshot } = require('./motorhomehire-profile-capture.cjs');
const { saveVehicle } = require('./wordpress-save.cjs');
const { loadSettings } = require('./catalog-plan.cjs');
const { root, readSiteConfig, captureFolder } = require('./project.cjs');
const { connectProfile } = require('./hidemium.cjs');

const SITE_ID = 'motorhomehireProfile';
const ledgerFile = (workspace, vehicleId) => store.inside(workspace, `catalog/create-requests/frontend-${vehicleId}.json`);

function reportBody(plan) {
  const d = plan.draft, i = d.identity;
  const lines = [`Frontend creation run: ${plan.runId}`, `Draft: ${plan.draftFile}`, `Catalog: ${d.vehicleId} (${i.country} / ${i.brand} / ${i.model}${i.variant ? ` / ${i.variant}` : ''})`,
    `Target: ${editor.addUrl}`, 'Action: create one new vehicle post and click Save Draft once. It is not published.',
    'Images are not uploaded; add the featured image and gallery manually afterwards.', ''];
  for (const jsonPath of schema.allPaths) {
    const value = store.get(d, schema.pointer(jsonPath));
    const sources = (d.sources[jsonPath] || []).map(s => s.type + (s.url ? ` ${s.url}` : s.site ? ` ${s.site} ${s.externalId}` : '')).join('; ');
    lines.push(`${jsonPath}: ${value === null ? '(left blank)' : JSON.stringify(value)}${sources ? `  [${sources}]` : ''}`);
  }
  lines.push('', 'Execution rechecks the live vehicle list for the same title, fills a fresh Add page,',
    'reads every control back, records the new post ID, then saves once. A failed or uncertain save',
    'is recovered from that post ID; Save is never clicked a second time.', 'Review this exact creation and change only the final status line.');
  return lines.join('\n') + '\n';
}
function createReport(workspace, draftFile) {
  return store.withLock(workspace, 'frontend-create-plans', () => {
    const checked = drafts.checkDraft(workspace, draftFile);
    if (checked.status !== 'ready') throw new Error(`The draft is ${checked.status}. Fix it until --check reports ready: ${path.relative(workspace, checked.file.replace(/\.json$/, '.report.txt'))}`);
    const draft = store.readJson(checked.file);
    const ledger = ledgerFile(workspace, draft.vehicleId);
    if (fs.existsSync(ledger)) {
      const previous = store.readJson(ledger), results = store.inside(workspace, `changes/${previous.runId}/results.json`);
      const status = fs.existsSync(results) ? store.readJson(results).status : 'pending';
      if (!['failedBeforeSave','notSaved'].includes(status)) throw new Error(`${draft.vehicleId} already has frontend creation run ${previous.runId} (${status}). Resume or recover that run instead.`);
    }
    const runId = 'FRONTEND-' + new Date().toISOString().replace(/\D/g, '') + '-' + randomUUID().slice(0, 8);
    const folder = store.inside(workspace, 'changes/' + runId);
    const plan = { schemaVersion: '1.0', runId, createdAt: new Date().toISOString(), draftFile: path.relative(workspace, checked.file).replace(/\\/g, '/'), draftHash: store.hash(draft), draft,
      config: readSiteConfig(SITE_ID, workspace), settings: loadSettings(workspace) };
    plan.planHash = store.hash(plan);
    store.atomicWrite(path.join(folder, 'plan.json'), plan);
    store.atomicWrite(path.join(folder, 'report.txt'), reportBody(plan) + '\nstatus: pending\n');
    store.atomicWrite(ledger, { vehicleId: draft.vehicleId, runId });
    return { plan, folder };
  });
}
function check(workspace, runId, { approved = true } = {}) {
  if (!/^FRONTEND-[a-zA-Z0-9_-]+$/.test(runId || '')) throw new Error('Use a generated FRONTEND run ID.');
  const folder = store.inside(workspace, 'changes/' + runId), plan = store.readJson(path.join(folder, 'plan.json'));
  const { planHash, ...payload } = plan;
  if (plan.runId !== runId || planHash !== store.hash(payload)) throw new Error('The frozen creation plan changed.');
  const report = fs.readFileSync(path.join(folder, 'report.txt'), 'utf8').replace(/^﻿/, '').replace(/\r\n?/g, '\n');
  const footer = /\nstatus: (pending|approved)\s*$/.exec(report);
  if (!footer || report.slice(0, footer.index) !== reportBody(plan)) throw new Error('The creation report changed beyond its status line.');
  if (approved && footer[1] !== 'approved') throw new Error('Review the creation report and set status: approved first.');
  if (!store.equal(plan.config, readSiteConfig(SITE_ID, workspace)) || !store.equal(plan.settings, loadSettings(workspace))) throw new Error('Creation settings changed after review.');
  const ledger = ledgerFile(workspace, plan.draft.vehicleId);
  if (!fs.existsSync(ledger) || store.readJson(ledger).runId !== runId) throw new Error('This vehicle\'s creation reservation belongs to another run.');
  if (store.hash(store.readJson(store.inside(workspace, plan.draftFile))) !== plan.draftHash) throw new Error('The draft JSON changed after the report. Prepare a new report.');
  return { plan, folder };
}
// A draft only has a preview link until it is published.
const publicUrl = snapshot => { const url = snapshot.capture.publicUrl; return url && !new URL(url).searchParams.has('preview') ? url : null; };
function incorporateCreated(workspace, plan, snapshot) {
  const postId = String(snapshot.capture.postId);
  fields.validateSnapshot(SITE_ID, snapshot, postId);
  return store.withLock(workspace, plan.draft.vehicleId, () => {
    const current = store.scanCurrent(workspace);
    for (const entry of current.values()) if (entry.record.sites[SITE_ID]?.records[postId] && entry.record.vehicleId !== plan.draft.vehicleId) throw new Error('The created post is linked to another catalog vehicle.');
    const entry = store.findCurrent(workspace, plan.draft.vehicleId), record = store.clone(entry.record);
    if (!store.equal(record.identity, plan.draft.identity)) throw new Error('Catalog identity changed during creation.');
    const site = record.sites[SITE_ID] ||= { availability: 'notLinked', records: {} };
    if (site.records[postId]) return entry.file;
    if (Object.keys(site.records).length) throw new Error('Another Motorhome Hire profile was linked to this vehicle during creation.');
    site.availability = 'linked';
    site.records[postId] = { mapping: { status: 'confirmed', role: 'primary', entityType: 'vehicle', siteOrigin: editor.SITE, editUrl: snapshot.capture.editUrl, publicUrl: publicUrl(snapshot) },
      observed: store.observation(snapshot), lastReadAttempt: { at: new Date().toISOString(), status: 'success', phase: 'afterCreate' } };
    record.catalogRevision++;
    return store.persist(workspace, fields.reconcile(record, plan.settings.policies), entry.file);
  });
}
const defaults = {
  connect: connectProfile,
  findTitle: editor.findTitle,
  openAdd: editor.openAdd,
  fill: (page, plan) => editor.fill(page, plan.draft),
  async submit(page, postId, timeout, log) {
    await saveVehicle(page, postId, { siteUrl: editor.SITE, timeout, log, retryClicks: false, buttonSelector: '#save-post',
      confirmationSelector: '#message.updated, #message.notice-success', confirmationText: /draft updated|post saved|post updated/i });
  },
  async status(page, postId, timeout) {
    await page.goto(editor.editUrl(postId), { waitUntil: 'load', timeout });
    const url = new URL(page.url());
    if (url.pathname !== '/wp-admin/post.php' || url.searchParams.get('post') !== String(postId)) throw new Error(`Post ${postId} editor did not open.`);
    return page.locator('[name="original_post_status"]').inputValue();
  },
  async capture(page, postId) {
    await mhEditor.targetEditor(page, { postId, editUrl: editor.editUrl(postId) });
    return page.evaluate(captureVehicleProfile, { fields: fieldMappings, specialFeatureLabels, expectedPostId: postId });
  },
};
async function execute(workspace, runId, { fillOnly = false, log = console.log, ...overrides } = {}) {
  const io = { ...defaults, ...overrides };
  return store.withAsyncLock(workspace, 'motorhomehire-frontend-create', async () => {
    const { plan, folder } = check(workspace, runId), resultFile = path.join(folder, 'results.json');
    let result = fs.existsSync(resultFile) ? store.readJson(resultFile) : { runId, planHash: plan.planHash, status: 'pending', vehicleId: plan.draft.vehicleId, attempted: false, postId: null };
    if (result.runId !== runId || result.planHash !== plan.planHash) throw new Error('Creation journal does not match the frozen plan.');
    if (result.status === 'done' || result.status === 'notSaved') return result;
    if (fillOnly && result.attempted) throw new Error('This creation was already submitted. Resume it with --run to recover.');
    const persist = () => { store.atomicWrite(resultFile, result); store.atomicWrite(path.join(folder, 'results.txt'), `Run: ${runId}\nStatus: ${result.status}\nVehicle: ${plan.draft.vehicleId}\nWordPress post ID: ${result.postId || 'not yet assigned'}\n${result.error || ''}\n`); };
    const lock = 'browser-' + store.hash({ profileId: plan.config.hidemiumProfileId, apiUrl: plan.config.hidemiumApiUrl || 'http://127.0.0.1:2222' }).slice(0, 24);
    return store.withAsyncLock(workspace, lock, async () => {
      const timeout = plan.config.pageTimeoutMs || 60000;
      let browser, page, keepPage = false;
      try {
        if (!result.attempted) {
          const recheck = drafts.checkDraft(workspace, plan.draftFile);
          if (recheck.status !== 'ready') throw new Error(`The draft is no longer ready (${recheck.status}): ${recheck.errors.join(' ')}`);
        }
        browser = await io.connect({ profileId: plan.config.hidemiumProfileId, apiUrl: plan.config.hidemiumApiUrl || 'http://127.0.0.1:2222', closeOnConnectFailure: false });
        page = await browser.contexts()[0].newPage();
        page.setDefaultTimeout(timeout);
        if (!result.attempted) {
          const existing = await io.findTitle(page, plan.draft.data.vehicle.name, timeout);
          if (existing.length) throw new Error(`A vehicle titled "${plan.draft.data.vehicle.name}" already exists on the website: post ${existing.map(row => row.postId).join(', ')}.`);
          const postId = await io.openAdd(page, timeout);
          const form = await io.fill(page, plan);
          store.atomicWrite(path.join(folder, 'before-form.json'), { postId, form });
          if (fillOnly) { result.status = 'filled'; result.filledPostId = postId; persist(); keepPage = true; log(`Draft filled and verified on the Add page (unsaved post ${postId}). The tab stays open for review; Save was not clicked.`); return result; }
          result.postId = postId; result.attempted = true; result.status = 'submitting'; result.submittedAt = new Date().toISOString(); persist();
          try { await io.submit(page, postId, timeout, log); } catch (error) { result.saveError = error.message; persist(); }
        }
        // Recover from the recorded post ID: an unsaved auto-draft means nothing was created.
        const status = await io.status(page, result.postId, timeout);
        if (status === 'auto-draft') {
          result.status = 'notSaved'; result.error = `WordPress did not save post ${result.postId}; it is still an unsaved auto-draft. Check the editor for errors, then prepare a new report.`; persist();
          return result;
        }
        const snapshot = await io.capture(page, result.postId);
        store.atomicWrite(path.join(folder, 'after-snapshot.json'), store.sanitize(snapshot));
        editor.verifySaved(plan.draft, snapshot);
        result.status = 'saved'; persist();
        storeCurrentSnapshot(snapshot, { postId: result.postId, country: plan.draft.identity.country }, captureFolder(SITE_ID, workspace));
        result.catalogFile = path.relative(workspace, incorporateCreated(workspace, plan, snapshot));
        result.status = 'done'; result.verifiedAt = new Date().toISOString(); result.publicUrl = publicUrl(snapshot); delete result.error; persist();
        return result;
      } catch (error) { result.status = result.attempted ? 'needsRecovery' : 'failedBeforeSave'; result.error = error.message; persist(); throw error; }
      finally { if (page && !keepPage && !page.isClosed()) await page.close(); if (browser) await browser.close(); }
    });
  });
}
async function main(args = process.argv.slice(2), workspace = root) {
  if (args.length === 1 && args[0] === '--help') { console.log('Create Motorhome Hire frontend profiles from a ready draft:\n  --report DRAFT.json\n  --check FRONTEND-RUN-ID\n  --fill FRONTEND-RUN-ID (fill for review; no Save)\n  --run FRONTEND-RUN-ID (one Save Draft, recovery, verification, catalog linking)'); return; }
  if (args.length !== 2 || !['--report','--check','--fill','--run'].includes(args[0])) throw new Error('Use --help for creation commands.');
  if (args[0] === '--report') { const r = createReport(workspace, args[1]); console.log(`Review: ${path.relative(workspace, r.folder)}/report.txt\nRun ID: ${r.plan.runId}`); return r; }
  if (args[0] === '--check') { const r = check(workspace, args[1]); console.log(`Approved creation: ${r.plan.draft.data.vehicle.name} (${r.plan.draft.vehicleId}).`); return r; }
  const r = await execute(workspace, args[1], { fillOnly: args[0] === '--fill' });
  console.log(`${r.status}: changes/${args[1]}/results.txt`); return r;
}
if (require.main === module) main().catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { reportBody, createReport, check, incorporateCreated, execute, main };
