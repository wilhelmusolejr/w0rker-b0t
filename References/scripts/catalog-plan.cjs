const fs = require('node:fs');
const path = require('node:path');
const { randomUUID } = require('node:crypto');
const store = require('./catalog-store.cjs');
const fields = require('./catalog-fields.cjs');
const gallery = require('./catalog-gallery.cjs');
const { hash, clone, equal, readJson, inside, atomicWrite, get, differences } = store;

function loadSettings(root) {
  const sites = readJson(inside(root, 'catalog/config/sites.json'));
  const policies = fields.validatePolicies(readJson(inside(root, 'catalog/config/field-policies.json')));
  if (sites.schemaVersion !== '1.0' || !sites.sites) throw new Error('Expected site settings schema 1.0.');
  for (const [siteId, settings] of Object.entries(sites.sites)) {
    if (!fields.SITES[siteId] || typeof settings.enabled !== 'boolean' || typeof settings.configFile !== 'string') throw new Error(`Unsupported site configuration: ${siteId}`);
    inside(root, settings.configFile);
  }
  return { sites, policies };
}
function selectTargets(record, selector, sites) {
  if (selector !== 'allLinked' && (!Array.isArray(selector) || !selector.length)) throw new Error('Choose targets: "allLinked" or a nonempty array of siteId/externalId selections.');
  const selections = selector === 'allLinked' ? Object.keys(record.sites).filter(site => Object.keys(record.sites[site].records).length).map(siteId => ({ siteId })) : selector;
  const targets = [], selected = new Set();
  for (const selection of selections) {
    if (!selection || typeof selection.siteId !== 'string' || Object.keys(selection).some(key => !['siteId','externalId','gallery'].includes(key))) throw new Error('Invalid target selection.');
    if (Object.hasOwn(selection, 'gallery') && (!gallery.SITE_IDS.includes(selection.siteId) || selection.externalId === undefined)) throw new Error('Gallery updates require an explicit Campervan Finder profile ID.');
    const { siteId, externalId } = selection;
    if (!fields.SITES[siteId] || !sites.sites[siteId]?.enabled) throw new Error(`Target is unavailable/disabled: ${siteId}.`);
    const records = record.sites[siteId]?.records || {};
    const ids = externalId === undefined ? Object.keys(records) : [String(externalId)];
    if (!ids.length) continue;
    if (ids.length > 1) throw new Error(`${record.vehicleId} has multiple ${siteId} records. Select their external IDs explicitly.`);
    for (const id of ids) {
      const external = records[id], key = `${siteId}:${id}`;
      if (!external) throw new Error(`Vehicle has no linked target ${key}.`);
      if (selected.has(key)) throw new Error(`Duplicate target: ${key}`);
      selected.add(key);
      const mapping = external.mapping;
      if (mapping.status !== 'confirmed' || !mapping.role || mapping.role === 'unassigned') throw new Error(`Confirm the mapping/role for ${record.vehicleId} -> ${key} before generating an executable job.`);
      if (mapping.siteOrigin !== fields.SITES[siteId].origin || mapping.entityType !== fields.SITES[siteId].entityType) throw new Error(`Mapped site/type does not match ${siteId}.`);
      fields.validateSnapshot(siteId, external.observed.snapshot, id);
      targets.push({ vehicleId: record.vehicleId, siteId, externalId: id, mapping: clone(mapping), baseline: clone(external.observed.snapshot), operations: [], notes: [], ...(Object.hasOwn(selection, 'gallery') ? { galleryRequest: clone(selection.gallery) } : {}) });
    }
  }
  if (!targets.length) throw new Error(`No linked enabled targets for ${record.vehicleId}.`);
  // A stable per-vehicle order makes capture-before-next-target behavior predictable.
  return targets.sort((a, b) => Object.keys(fields.SITES).indexOf(a.siteId) - Object.keys(fields.SITES).indexOf(b.siteId));
}
function validateProposal(current, proposed) {
  store.validateRecord(proposed, { proposed: true });
  if (current.exampleOnly || proposed.exampleOnly) throw new Error('Documentation examples cannot be executed. Import reviewed production sources first.');
  if (proposed.catalogRevision !== current.catalogRevision) throw new Error('Proposed baseline revision is stale. Rebase the edits onto a fresh current copy, or resume its already frozen run.');
  const mask = record => {
    const copy = clone(record);
    for (const site of Object.values(copy.sites)) for (const external of Object.values(site.records)) delete external.observed.snapshot;
    for (const fact of Object.values(copy.sharedFacts)) fact.value = null;
    return copy;
  };
  if (!equal(mask(current), mask(proposed))) throw new Error('Proposal changed identity, mapping, revision, evidence or capture metadata. Copy current and edit only supported snapshot data/shared-fact values.');
}
function addOperation(target, operation) {
  fields.mappingFor(target.siteId, operation.path, target.baseline, operation.value);
  const existing = target.operations.find(op => op.path === operation.path);
  if (existing) {
    if (!equal(existing.value, operation.value)) throw new Error(`Conflicting site/shared instructions for ${target.siteId}:${target.externalId}${operation.path}.`);
    existing.sources.push(...operation.sources);
  } else target.operations.push(operation);
}
function planVehicle(current, proposed, task, settings, galleryInputs = []) {
  validateProposal(current, proposed);
  const targets = selectTargets(current, task.targets, settings.sites);
  for (const target of targets) if (Object.hasOwn(target, 'galleryRequest')) {
    if (task.vehicleIds?.length !== 1 || task.vehicleIds[0] !== current.vehicleId) throw new Error('Gallery tasks require exactly one explicit vehicleId.');
    const frozen = galleryInputs.find(input => input.siteId === target.siteId && input.externalId === target.externalId);
    if (!frozen) throw new Error('Missing frozen gallery inputs.');
    target.gallery = gallery.resolve(target.galleryRequest, frozen.assets, target.baseline);
    delete target.galleryRequest;
  }
  const selected = new Map(targets.map(target => [`${target.siteId}:${target.externalId}`, target]));
  for (const [siteId, site] of Object.entries(current.sites)) for (const [externalId, external] of Object.entries(site.records)) {
    const desired = proposed.sites[siteId].records[externalId].observed.snapshot;
    const edits = differences(external.observed.snapshot, desired);
    if (!edits.length) continue;
    const target = selected.get(`${siteId}:${externalId}`);
    if (!target) throw new Error(`Edited field is outside selected targets: ${siteId}:${externalId}.`);
    for (const edit of edits) addOperation(target, { ...edit, sources: [{ type: 'site' }] });
  }
  const requestedFacts = [];
  for (const [fact, state] of Object.entries(current.sharedFacts)) {
    const desired = proposed.sharedFacts[fact].value;
    if (equal(state.value, desired)) continue;
    requestedFacts.push({ fact, before: clone(state.value), value: clone(desired) });
    let supported = 0;
    for (const target of targets) {
      const rendered = fields.renderFact(fact, desired, target, settings.policies);
      supported += rendered.operations.length;
      for (const operation of rendered.operations) addOperation(target, operation);
      target.notes.push(...rendered.notes);
    }
    if (!supported) throw new Error(`No selected target can apply shared fact ${fact}.`);
  }
  if (task.requireCompleteCoverage && targets.some(target => target.notes.length)) throw new Error('Shared field coverage is incomplete: ' + targets.flatMap(target=>target.notes.map(note=>`${target.siteId}:${target.externalId} ${note.fact}: ${note.reason}`)).join('; '));
  return { vehicleId: current.vehicleId, baseCatalogRevision: current.catalogRevision, requestedFacts, targets };
}
function derive(job, inputs, settings) {
  if (!['1.0','1.1'].includes(job?.schemaVersion) || job.exampleOnly || !/^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$/.test(job.jobId || '') || !Array.isArray(job.tasks) || !job.tasks.length) throw new Error('Expected a production job schema 1.0/1.1 with jobId/tasks.');
  if (!['stop','continueIndependent'].includes(job.failurePolicy)) throw new Error('failurePolicy must be stop or continueIndependent.');
  const tasks = [], seenTasks = new Set(), seenVehicles = new Set();
  if (Object.keys(job).some(key=>!['schemaVersion','jobId','failurePolicy','tasks','exampleOnly','notes'].includes(key))) throw new Error('Unknown job property.');
  for (const task of job.tasks) {
    if (!/^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$/.test(task.taskId || '') || seenTasks.has(task.taskId)) throw new Error('Task IDs must be unique.');
    seenTasks.add(task.taskId);
    if (Object.keys(task).some(key=>!['taskId','enabled','action','source','targets','vehicleIds','dependsOn','requireCompleteCoverage','notes'].includes(key))) throw new Error('Unknown update task property.');
    if (task.enabled !== undefined && typeof task.enabled !== 'boolean') throw new Error('enabled must be boolean.');
    if (task.enabled === false) continue;
    if (task.action !== 'update') throw new Error(`Only update is implemented in this phase; unsupported action: ${task.action}.`);
    if (job.schemaVersion !== '1.1' && Array.isArray(task.targets) && task.targets.some(target => target && Object.hasOwn(target, 'gallery'))) throw new Error('Gallery settings require job schemaVersion 1.1.');
    if (task.source?.type !== 'proposedFolder' || typeof task.source.path !== 'string') throw new Error('Update source must be a proposedFolder.');
    if (task.requireCompleteCoverage !== undefined && typeof task.requireCompleteCoverage !== 'boolean') throw new Error('requireCompleteCoverage must be boolean.');
    if (task.dependsOn !== undefined && !Array.isArray(task.dependsOn)) throw new Error('dependsOn must be an array.');
    for (const dependency of task.dependsOn || []) if (!tasks.some(prior => prior.taskId === dependency)) throw new Error('Dependencies must reference earlier enabled tasks.');
    const selectedInputs = inputs.filter(input => input.taskId === task.taskId);
    if (!selectedInputs.length) throw new Error(`No proposed vehicles found for ${task.taskId}.`);
    const vehicles = selectedInputs.map(input => {
      if (seenVehicles.has(input.current.vehicleId)) throw new Error('Select each vehicle in one update task per job; combine its target selections there.');
      seenVehicles.add(input.current.vehicleId);
      return planVehicle(input.current, input.proposed, task, settings, input.galleryInputs);
    });
    tasks.push({ taskId: task.taskId, dependsOn: task.dependsOn || [], vehicles });
  }
  if (!tasks.length) throw new Error('No enabled update tasks.');
  return { tasks, failurePolicy: job.failurePolicy };
}
function collectInputs(root, job) {
  const current = store.scanCurrent(root), inputs = [];
  for (const task of job.tasks || []) {
    if (task.enabled === false) continue;
    if (task.action !== 'update' || task.source?.type !== 'proposedFolder') throw new Error('This runner accepts only update tasks with proposedFolder sources.');
    const proposedRoot = inside(root, 'catalog/proposed');
    const source = inside(root, task.source.path);
    inside(proposedRoot, path.relative(proposedRoot, source));
    if (task.vehicleIds !== undefined && (!Array.isArray(task.vehicleIds) || !task.vehicleIds.length || new Set(task.vehicleIds).size !== task.vehicleIds.length || task.vehicleIds.some(id => !store.ID.test(id)))) throw new Error('vehicleIds must be a nonempty unique array of permanent IDs.');
    const found = new Set();
    const galleryInputs = gallery.prepareInputs(root, task);
    for (const file of store.jsonFiles(source)) {
      const proposed = readJson(file);
      if (task.vehicleIds && !task.vehicleIds.includes(proposed.vehicleId)) continue;
      const entry = current.get(proposed.vehicleId);
      if (!entry) throw new Error(`Update input has no current counterpart: ${path.basename(file)}. Creation is an explicit separate action.`);
      if (found.has(proposed.vehicleId)) throw new Error('Duplicate proposed vehicle ID.');
      if (path.basename(file) !== path.basename(entry.file) || path.basename(path.dirname(file)) !== proposed.identity?.country) throw new Error('Keep the current filename and country directory in proposed.');
      found.add(proposed.vehicleId);
      inputs.push({ taskId: task.taskId, currentFile: path.relative(root, entry.file), proposedFile: path.relative(root, file), current: entry.record, proposed, ...(galleryInputs.length ? { galleryInputs } : {}) });
    }
    if (task.vehicleIds?.some(id => !found.has(id))) throw new Error('Selected vehicle ID was not found in proposed.');
  }
  return inputs;
}
function reportBody(plan) {
  const lines = [`Catalog run: ${plan.runId}`, `Job: ${plan.job.jobId}`, 'Action: update', `Failure policy: ${plan.derived.failurePolicy}`, 'Before-values are frozen catalog observations; execution reads the website again.', ''];
  for (const task of plan.derived.tasks) {
    lines.push(`TASK: ${task.taskId}`);
    for (const vehicle of task.vehicles) {
      lines.push(`VEHICLE: ${vehicle.vehicleId}`);
      for (const fact of vehicle.requestedFacts) lines.push(`SHARED: ${fact.fact} ${JSON.stringify(fact.before)} -> ${JSON.stringify(fact.value)}`);
      for (const target of vehicle.targets) {
        lines.push(`TARGET: ${fields.SITES[target.siteId].label} (${target.siteId}:${target.externalId})`, `Editor: ${target.baseline.capture?.editUrl || target.baseline.source.url}`);
        if (!target.operations.length && !target.gallery) lines.push('  Refresh only: no requested field changes.');
        if (target.gallery) lines.push(...gallery.reportLines(target.gallery));
        for (const operation of target.operations) lines.push(`  CHANGE: ${operation.path}`, `    Current: ${JSON.stringify(operation.before)}`, `    Proposed: ${JSON.stringify(operation.value)}`);
        for (const note of target.notes) lines.push(`  COVERAGE: ${note.fact}${note.path || ''}: ${note.reason}`);
      }
      lines.push('');
    }
  }
  lines.push('Review exact targets and values. Change only the final status line to approve.');
  return lines.join('\n') + '\n';
}
function createPlan(root, jobFile) {
  const jobPath = inside(root, jobFile), job = readJson(jobPath), settings = loadSettings(root);
  const inputs = collectInputs(root, job), derived = derive(job, inputs, settings);
  const runId = `RUN-${new Date().toISOString().replace(/\D/g, '')}-${randomUUID().slice(0, 8)}`;
  const folder = inside(root, `changes/${runId}`);
  const plan = { schemaVersion: '1.0', runId, createdAt: new Date().toISOString(), jobFile: path.relative(root, jobPath), job, settings, inputs, derived };
  plan.planHash = hash(plan);
  const body = reportBody(plan);
  fs.mkdirSync(path.dirname(folder), { recursive: true });
  fs.mkdirSync(folder, { recursive: false });
  atomicWrite(path.join(folder, 'plan.json'), plan);
  atomicWrite(path.join(folder, 'job.json'), job);
  for (const input of inputs) {
    atomicWrite(path.join(folder, 'inputs/current', input.current.vehicleId + '.json'), input.current);
    atomicWrite(path.join(folder, 'inputs/proposed', input.proposed.vehicleId + '.json'), input.proposed);
  }
  atomicWrite(path.join(folder, 'review.json'), { planHash: plan.planHash, bodyHash: hash(body) });
  atomicWrite(path.join(folder, 'report.txt'), body + '\nstatus: pending\n');
  atomicWrite(inside(root, 'catalog/config/selection.json'), { runId });
  return { plan, folder, reportFile: path.join(folder, 'report.txt') };
}
function checkPlan(root, runId, { approved = true } = {}) {
  if (!/^RUN-[a-zA-Z0-9_-]+$/.test(runId || '')) throw new Error('Pass a generated RUN ID.');
  const folder = inside(root, `changes/${runId}`), plan = readJson(path.join(folder, 'plan.json'));
  const { planHash, ...content } = plan;
  if (plan.runId !== runId || hash(content) !== planHash) throw new Error('Frozen plan changed. Generate and review another report.');
  if (!equal(derive(plan.job, plan.inputs, plan.settings), plan.derived)) throw new Error('Frozen operations do not match their inputs.');
  const review = readJson(path.join(folder, 'review.json'));
  const text = fs.readFileSync(path.join(folder, 'report.txt'), 'utf8').replace(/^\uFEFF/, '').replace(/\r\n?/g, '\n');
  const footer = /\nstatus: (pending|approved)\s*$/.exec(text);
  if (!footer || hash(text.slice(0, footer.index)) !== review.bodyHash || planHash !== review.planHash) throw new Error('Report content changed beyond its approval line. Generate another report.');
  if (approved && footer[1] !== 'approved') throw new Error('Report is pending. Review it and change the final line to status: approved.');
  if (!equal(readJson(inside(root, plan.jobFile)), plan.job) || !equal(loadSettings(root), plan.settings)) throw new Error('Job/site settings/field policies changed since report generation.');
  gallery.checkInputs(root, plan);
  for (const input of plan.inputs) if (!fs.existsSync(inside(root, input.proposedFile)) || !equal(readJson(inside(root, input.proposedFile)), input.proposed)) {
    const resultsFile = path.join(folder, 'results.json');
    const completed = fs.existsSync(resultsFile) && readJson(resultsFile).completedVehicleIds?.includes(input.current.vehicleId);
    if (!completed) throw new Error('Proposed input changed or disappeared after review.');
  }
  return { plan, folder, reportFile: path.join(folder, 'report.txt') };
}

module.exports = { loadSettings, selectTargets, validateProposal, addOperation, planVehicle, derive, collectInputs, reportBody, createPlan, checkPlan };
