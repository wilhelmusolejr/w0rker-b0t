const fs = require('node:fs');
const path = require('node:path');
const { randomUUID } = require('node:crypto');
const store = require('./catalog-store.cjs');
const fields = require('./catalog-fields.cjs');
const { checkPlan } = require('./catalog-plan.cjs');
const adapters = require('./catalog-adapters.cjs');
const hidemium = require('./hidemium.cjs');
const gallery = require('./catalog-gallery.cjs');
const { storeCapture } = require('./capture-profile.cjs');

const completed = status => ['saved','alreadySatisfied','refreshed'].includes(status);
const keyOf = target => `${target.vehicleId}:${target.siteId}:${target.externalId}`;
function connection(root, siteId, settings) {
  const value = store.readJson(store.inside(root, settings.sites.sites[siteId].configFile));
  if (typeof value.hidemiumProfileId !== 'string' || !value.hidemiumProfileId.trim()) throw new Error(`Configure the Hidemium profile for ${siteId}.`);
  const apiUrl = value.hidemiumApiUrl || 'http://127.0.0.1:2222';
  if (!['http:','https:'].includes(new URL(apiUrl).protocol)) throw new Error('Hidemium API URL must be HTTP/HTTPS.');
  const timeout = value.pageTimeoutMs ?? 300000;
  if (!Number.isInteger(timeout) || timeout < 1000 || timeout > 600000) throw new Error('pageTimeoutMs must be between 1000 and 600000.');
  return { hidemiumProfileId: value.hidemiumProfileId, hidemiumApiUrl: apiUrl, pageTimeoutMs: timeout };
}
function sessions({ connect = hidemium.connectProfile, close = hidemium.closeAutomation, prepare = hidemium.prepareAutomationTab } = {}) {
  let active;
  return {
    async page(config) {
      const key = `${config.hidemiumApiUrl}|${config.hidemiumProfileId}`;
      if (active && active.key !== key) { const prior = active; active = null; await close(prior.browser, prior.pages, prior.config); }
      if (!active) {
        const browser = await connect({ profileId: config.hidemiumProfileId, apiUrl: config.hidemiumApiUrl });
        active = { key, browser, pages: [], config };
        active.page = await prepare(browser, active.pages);
      }
      if (active.page.isClosed()) active.page = await prepare(active.browser, active.pages);
      return active.page;
    },
    async close() { if (active) { const prior = active; active = null; await close(prior.browser, prior.pages, prior.config); } },
  };
}
async function withProfileLocks(root, configurations, work) {
  const keys = [...new Set(Object.values(configurations).map(config => 'browser-' + store.hash({ profileId:config.hidemiumProfileId, apiUrl:config.hidemiumApiUrl }).slice(0,24)))].sort();
  const next = index => index === keys.length ? work() : store.withAsyncLock(root,keys[index],()=>next(index+1));
  return next(0);
}
function cleanup(root, folder, input) {
  const source = store.inside(root, input.proposedFile);
  const proposedRoot = store.inside(root, 'catalog/proposed');
  store.inside(proposedRoot, path.relative(proposedRoot, source));
  const archive = store.inside(folder, `completed/proposed/${input.current.identity.country}/${path.basename(source)}`);
  if (!fs.existsSync(source)) {
    if (fs.existsSync(archive) && store.equal(store.readJson(archive), input.proposed)) return { status: 'archived', file: path.relative(root, archive) };
    throw new Error('Completed proposed input disappeared without its archive.');
  }
  if (!store.equal(store.readJson(source), input.proposed)) return { status: 'retainedChanged', reason: 'Proposed file changed during execution; its newer edits remain pending.' };
  if (fs.existsSync(archive)) throw new Error('A proposed archive already exists; it will not be overwritten.');
  fs.mkdirSync(path.dirname(archive), { recursive: true });
  fs.renameSync(source, archive);
  if (!store.equal(store.readJson(archive), input.proposed)) {
    if (!fs.existsSync(source)) fs.renameSync(archive, source);
    throw new Error(`Proposed input changed while archiving; recover it from ${archive} if it could not be restored.`);
  }
  return { status: 'archived', file: path.relative(root, archive) };
}
async function run(root, runId, { process = adapters.processTarget, capture = adapters.capture, log = console.log, sessionFactory = sessions, ...sessionOptions } = {}) {
  return store.withAsyncLock(root, runId, async () => {
    const { plan, folder } = checkPlan(root, runId);
    const resultFile = path.join(folder, 'results.json');
    const targets = plan.derived.tasks.flatMap(task => task.vehicles.flatMap(vehicle => vehicle.targets));
    const configurations = Object.fromEntries([...new Set(targets.map(target => target.siteId))].map(siteId => [siteId, connection(root, siteId, plan.settings)]));
    for (const target of targets) {
      const current = store.findCurrent(root, target.vehicleId).record.sites[target.siteId]?.records[target.externalId];
      if (!current || !store.equal(current.mapping, target.mapping)) throw new Error('Current target mapping changed after review.');
    }
    return withProfileLocks(root, configurations, async () => {
    const result = fs.existsSync(resultFile) ? store.readJson(resultFile) : {
      schemaVersion: '1.0', runId, planHash: plan.planHash, startedAt: new Date().toISOString(), status: 'pending',
      targets: targets.map(target => ({ key: keyOf(target), vehicleId: target.vehicleId, siteId: target.siteId, externalId: target.externalId, status: 'pending', attempts: 0, captures: [], errors: [] })),
      completedVehicleIds: [], cleanup: {},
    };
    if (result.planHash !== plan.planHash || result.runId !== runId || !store.equal(result.targets.map(item => item.key), targets.map(keyOf))) throw new Error('Recovery results do not match this plan.');
    const persist = () => { result.updatedAt = new Date().toISOString(); store.atomicWrite(resultFile, result); };
    const browser = sessionFactory(sessionOptions);
    const byKey = new Map(result.targets.map(item => [item.key, item]));
    const taskStatus = new Map();
    let stopped = false, executionError;
    result.status = 'running'; delete result.reason; persist();
    try {
      for (const task of plan.derived.tasks) {
        if (stopped || task.dependsOn.some(dep => taskStatus.get(dep) !== 'done')) { taskStatus.set(task.taskId, 'skipped'); continue; }
        for (const vehicle of task.vehicles) {
          for (const target of vehicle.targets) {
            const outcome = byKey.get(keyOf(target));
            if (completed(outcome.status)) continue;
            if (stopped) break;
            const config = configurations[target.siteId];
            let galleryContext;
            if (target.gallery) {
              outcome.gallery ||= gallery.journalFor(target);
              gallery.validateJournal(target, outcome.gallery);
              galleryContext = { root, journal: store.clone(outcome.gallery), persistJournal(journal) {
                gallery.validateJournal(target, journal);
                outcome.gallery = store.clone(journal); persist();
              } };
            }
            outcome.attempts++; outcome.status = 'running'; persist();
            let page, lastPhase, lastBefore;
            const onCapture = (snapshot, phase) => {
              fields.validateSnapshot(target.siteId, snapshot, target.externalId);
              const file = store.inside(folder, `captures/${target.vehicleId}/${target.siteId}-${target.externalId}-${phase}-${randomUUID()}.json`);
              // Persist recovery capture first in case the catalog write fails after a website save.
              store.atomicWrite(file, store.sanitize(snapshot));
              const incorporated = store.incorporate(root, target, snapshot, { phase, reconcile: record => fields.reconcile(record, plan.settings.policies) });
              outcome.captures.push({ phase, file: path.relative(root, file), catalogRevision: incorporated.record.catalogRevision, captureHash: store.hash(store.sanitize(snapshot)) });
              lastPhase = phase; persist();
              log(`${target.vehicleId}: captured ${fields.SITES[target.siteId].label} ${target.externalId} (${phase}); current revision ${incorporated.record.catalogRevision}.`);
              if (phase === 'before') lastBefore = store.clone(snapshot);
              if (phase === 'after') {
                fields.checkOperations(target,snapshot,{desired:true});
                if (target.gallery) gallery.checkSnapshot(target, snapshot, outcome.gallery, { desired: true });
                if (!lastBefore) throw new Error('Adapter omitted its before capture.');
                adapters.checkPreserved(target,lastBefore,snapshot);
                // Keep captures/<site>/current in step with the verified saved profile.
                storeCapture(target.siteId, snapshot, { externalId: target.externalId, country: incorporated.record.identity.country, root });
              }
            };
            try {
              await store.withAsyncLock(root, `target-${target.siteId}-${target.externalId}`, async () => {
                page = await browser.page(config);
                log(`${target.vehicleId}: updating ${fields.SITES[target.siteId].label} ${target.externalId}.`);
                const processed = await process(page, target, { timeout: config.pageTimeoutMs, log, onCapture, galleryContext });
                if (lastPhase !== 'after' || !completed(processed.status)) throw new Error('Adapter did not persist a verified post-save capture.');
                outcome.status = processed.status;
              });
            } catch (error) {
              outcome.status = 'failed'; outcome.errors.push(error.message);
              // Capture actual server state even after an ambiguous save/verification failure.
              if (page) try { onCapture(await capture(page, target, { timeout: config.pageTimeoutMs, log }), 'recovery'); }
              catch (readError) {
                outcome.errors.push(`Recovery capture: ${readError.message}`);
                try { store.recordReadFailure(root, target, readError); } catch (writeError) { outcome.errors.push(`Read failure could not be recorded: ${writeError.message}`); }
              }
              log(`${target.vehicleId}: ${target.siteId}:${target.externalId} failed: ${error.message}`);
              if (plan.derived.failurePolicy === 'stop') stopped = true;
            }
            persist();
          }
        }
        taskStatus.set(task.taskId, task.vehicles.every(vehicle => vehicle.targets.every(target => completed(byKey.get(keyOf(target)).status))) ? 'done' : 'failed');
      }
    } catch (error) { executionError = error; }
    finally { try { await browser.close(); } catch (error) { executionError = new Error(`${executionError ? executionError.message + ' | ' : ''}Browser cleanup failed: ${error.message}`); } }
    result.completedVehicleIds = [...new Set(plan.derived.tasks.flatMap(task => task.vehicles.filter(vehicle => vehicle.targets.every(target => completed(byKey.get(keyOf(target)).status))).map(vehicle => vehicle.vehicleId)))];
    persist();
    for (const input of plan.inputs) if (result.completedVehicleIds.includes(input.current.vehicleId) && result.cleanup[input.current.vehicleId]?.status !== 'archived') {
      try { result.cleanup[input.current.vehicleId] = cleanup(root, folder, input); }
      catch (error) { result.cleanup[input.current.vehicleId] = { status: 'failed', reason: error.message }; }
    }
    result.coverage = targets.some(target => target.notes.length) ? 'partial' : 'completeForRequestedFields';
    result.status = !executionError && result.targets.every(item => completed(item.status)) && !Object.values(result.cleanup).some(item => item.status === 'failed') ? 'done' : 'failed';
    result.reason = executionError?.message || (result.status === 'failed' ? 'Some targets or input archives remain incomplete. Resume this RUN ID to retry its frozen operations.' : null);
    result.finishedAt = new Date().toISOString(); persist();
    store.atomicWrite(path.join(folder, 'results.txt'), [`Run: ${runId}`, `Status: ${result.status}`, `Field coverage: ${result.coverage}`,
      ...result.targets.map(item => `${item.vehicleId} ${item.siteId}:${item.externalId}: ${item.status}${item.errors.length ? ' (' + item.errors.at(-1) + ')' : ''}`),
      ...Object.entries(result.cleanup).map(([id, state]) => `${id} proposal: ${state.status}`), result.reason || '', ''].join('\n'));
    const selectionFile = store.inside(root,'catalog/config/selection.json');
    if (result.status === 'done' && fs.existsSync(selectionFile) && store.readJson(selectionFile).runId === runId) store.atomicWrite(selectionFile,{runId:''});
    return result;
    });
  });
}

module.exports = { completed, keyOf, connection, sessions, withProfileLocks, cleanup, run };
