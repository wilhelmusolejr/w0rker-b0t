// Local review hub: one page listing everything waiting for the user's review
// (catalog update reports and multi-source comparison bundles), with before/after
// comparisons, an editable proposal form, a recorded decision and a save queue.
// Listens on 127.0.0.1 only. On the user's request it approves a report's status
// line, rebuilds the report from their edits, or saves approved reports to the
// website one at a time with the same catalog-update.cjs --check and --run.
// See "Review hub/AGENTS.md".
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { randomBytes, createHash } = require('node:crypto');
const { spawn } = require('node:child_process');
const store = require('./catalog-store.cjs');
const fields = require('./catalog-fields.cjs');
const { checkPlan, createPlan } = require('./catalog-plan.cjs');
const SC = require('../Source comparison/source-compare.js');
const { root } = require('./project.cjs');

const RUN_ID = /^RUN-[a-zA-Z0-9_-]+$/;
const BUNDLE_FOLDERS = ['jobs/frontend-drafts', 'jobs/source-bundles'];
const STATIC = { '/hub/': 'Review hub', '/source-comparison/': 'Source comparison' };
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.png': 'image/png', '.gif': 'image/gif', '.webp': 'image/webp' };
const MAX_BODY = 5 * 1024 * 1024;

class HttpError extends Error { constructor(status, message) { super(message); this.status = status; } }
const readOptional = file => fs.existsSync(file) ? store.readJson(file) : null;
const runFolder = (workspace, runId) => {
  if (!RUN_ID.test(runId || '')) throw new HttpError(400, 'Invalid RUN ID.');
  const folder = store.inside(workspace, `changes/${runId}`);
  if (!fs.existsSync(path.join(folder, 'plan.json'))) throw new HttpError(404, `Unknown run: ${runId}`);
  return folder;
};
const reportStatus = folder => {
  const text = fs.readFileSync(path.join(folder, 'report.txt'), 'utf8').replace(/\r\n?/g, '\n');
  return /\nstatus: (pending|approved)\s*$/.exec(text)?.[1] || 'invalid';
};
const targetsOf = plan => plan.derived.tasks.flatMap(task => task.vehicles.flatMap(vehicle => vehicle.targets));
// Short names for the list tags.
const WEBSITES = { motorhomehireProfile: 'Motorhome Hire', motorhomehireAdmin: 'Motorhome Hire search', campervanfinderAuProfile: 'Campervan Finder', campervanfinderNzProfile: 'Campervan Finder' };
const COUNTRIES = { 'new zealand': 'nz', australia: 'au' };
const unique = values => [...new Set(values.filter(Boolean))];

// One state per run, in the order the user acts on them.
function runState(workspace, runId) {
  const folder = runFolder(workspace, runId);
  const results = readOptional(path.join(folder, 'results.json')), decision = readOptional(path.join(folder, 'decision.json'));
  if (results) return { state: ['done', 'failed'].includes(results.status) ? results.status : 'running', results, decision };
  const status = reportStatus(folder);
  if (status === 'approved') return { state: 'readyToRun', decision };
  const superseded = readOptional(path.join(folder, 'superseded.json'));
  try { checkPlan(workspace, runId, { approved: false }); }
  catch (error) { return superseded ? { state: 'replaced', reason: `Replaced by ${superseded.supersededBy} with your edits.`, supersededBy: superseded.supersededBy, decision } : { state: 'stale', reason: error.message, decision }; }
  return { state: decision?.decision === 'changesRequested' ? 'changesRequested' : 'needsReview', decision };
}

const identityOf = (plan, vehicleId) => plan.inputs.find(input => input.current.vehicleId === vehicleId)?.current.identity || null;
function vehicleLabel(plan, vehicleId) {
  const identity = identityOf(plan, vehicleId);
  return identity ? `${identity.brand} ${identity.model} (${identity.country.toUpperCase()})` : vehicleId;
}
function runTags(plan, targets) {
  const identities = unique(targets.map(target => target.vehicleId)).map(id => identityOf(plan, id)).filter(Boolean);
  return { websites: unique(targets.map(target => WEBSITES[target.siteId] || fields.SITES[target.siteId]?.label || target.siteId)),
    brands: unique(identities.map(identity => identity.brand)), countries: unique(identities.map(identity => identity.country?.toUpperCase())) };
}

function listRuns(workspace) {
  const changes = store.inside(workspace, 'changes');
  if (!fs.existsSync(changes)) return [];
  return fs.readdirSync(changes, { withFileTypes: true })
    .filter(entry => entry.isDirectory() && RUN_ID.test(entry.name) && fs.existsSync(path.join(changes, entry.name, 'plan.json')))
    .map(entry => {
      const plan = store.readJson(path.join(changes, entry.name, 'plan.json')), state = runState(workspace, entry.name);
      const targets = targetsOf(plan);
      const identities = unique(targets.map(target => target.vehicleId)).map(id => identityOf(plan, id)).filter(Boolean);
      return { kind: 'update', id: plan.runId, createdAt: plan.createdAt, jobId: plan.job.jobId, state: state.state, reason: state.reason || null,
        title: identities.map(identity => `${identity.brand} ${identity.model}`).join(', ') || plan.job.jobId, tags: runTags(plan, targets),
        hasGallery: targets.some(target => target.gallery), supersededBy: state.supersededBy || null,
        vehicles: [...new Set(targets.map(target => target.vehicleId))].map(id => vehicleLabel(plan, id)),
        sites: [...new Set(targets.map(target => fields.SITES[target.siteId]?.label || target.siteId))],
        changes: targets.reduce((n, target) => n + target.operations.length + (target.gallery ? 1 : 0), 0),
        decidedAt: state.decision?.decidedAt || null };
    })
    .sort((a, b) => String(b.createdAt).localeCompare(String(a.createdAt)));
}

function bundleFiles(workspace) {
  return BUNDLE_FOLDERS.flatMap(folder => {
    const absolute = store.inside(workspace, folder);
    if (!fs.existsSync(absolute)) return [];
    return fs.readdirSync(absolute).filter(name => name.endsWith('.sources.json')).map(name => `${folder}/${name}`);
  });
}
function bundlePath(workspace, file) {
  const relative = String(file || '').replace(/\\/g, '/');
  if (!bundleFiles(workspace).includes(relative)) throw new HttpError(404, 'Unknown comparison bundle.');
  return store.inside(workspace, relative);
}
const reviewedPath = file => file.replace(/\.sources\.json$/, '.sources.reviewed.json');

function listBundles(workspace) {
  return bundleFiles(workspace).map(file => {
    const absolute = store.inside(workspace, file), bundle = store.readJson(absolute), reviewed = readOptional(reviewedPath(absolute));
    let conflicts = 0;
    try { conflicts = SC.compareBundle(bundle).filter(entry => entry.state === 'conflict').length; } catch { conflicts = null; }
    const state = reviewed?.review?.status === 'complete' ? 'reviewed' : reviewed ? 'inProgress' : 'needsReview';
    let identity = null;
    try { identity = bundle.targetVehicle?.vehicleId ? store.findCurrent(workspace, bundle.targetVehicle.vehicleId).record.identity : null; } catch { identity = null; }
    const country = identity?.country || COUNTRIES[String(bundle.targetVehicle?.market || '').toLowerCase()] || null;
    return { kind: 'newVehicle', id: file, name: bundle.targetVehicle?.workingName || path.basename(file), market: bundle.targetVehicle?.market || null,
      tags: { websites: ['Motorhome Hire'], brands: unique([identity?.brand]), countries: unique([country?.toUpperCase()]) },
      vehicleId: bundle.targetVehicle?.vehicleId || null, sources: (bundle.sources || []).length, conflicts, state,
      createdAt: fs.statSync(reviewed ? reviewedPath(absolute) : absolute).mtime.toISOString() };
  }).sort((a, b) => b.createdAt.localeCompare(a.createdAt));
}

function labelFor(target, operationPath) {
  const tab = /^\/data\/productTabs\/tabs\/([^/]+)\/content$/.exec(operationPath);
  if (tab) return `${target.baseline.data?.productTabs?.tabs?.[tab[1]]?.title || 'Tab'} tab`;
  return operationPath.split('/').filter(Boolean).slice(-1)[0].replace(/[_-]+/g, ' ').replace(/\b\w/g, c => c.toUpperCase());
}

// Every image in the frozen upload folder, including ones the user left out.
function folderFiles(plan, target) {
  const input = plan.inputs.find(entry => entry.current.vehicleId === target.vehicleId);
  return input?.galleryInputs?.find(entry => entry.siteId === target.siteId && entry.externalId === target.externalId)?.assets.files || [];
}
function galleryDetail(plan, target) {
  const g = target.gallery, previews = {};
  for (const item of target.baseline.audit?.galleryPreviews || []) if (Number.isSafeInteger(item?.attachmentId) && /^https?:\/\//i.test(item.url || '')) previews[item.attachmentId] = item.url;
  return { baselineIds: g.baselineIds, remove: g.remove, retainedIds: g.retainedIds, previews,
    uploads: g.uploads.map(({ filename, size, sha256 }) => ({ filename, size, sha256 })),
    folder: folderFiles(plan, target).map(({ filename, size }) => ({ filename, size })),
    finalOrder: g.finalOrder || [...g.retainedIds.map(attachmentId => ({ attachmentId })), ...g.uploads.map(({ filename }) => ({ filename }))],
    skippedUploads: g.skippedUploads || [],
    orderSource: g.orderSource ? { file: g.orderSource.file, group: g.orderSource.group } : null };
}

function runDetail(workspace, runId) {
  const folder = runFolder(workspace, runId), plan = store.readJson(path.join(folder, 'plan.json')), state = runState(workspace, runId);
  const targets = targetsOf(plan).map((target, index) => {
    const capture = target.baseline.capture || {};
    return { index, key: `${target.siteId}:${target.externalId}`, vehicleId: target.vehicleId, vehicle: vehicleLabel(plan, target.vehicleId),
      siteId: target.siteId, site: fields.SITES[target.siteId]?.label || target.siteId, externalId: target.externalId,
      editUrl: capture.editUrl || target.baseline.source?.url || null, publicUrl: capture.publicUrl || target.mapping?.publicUrl || null,
      siteOrigin: target.mapping?.siteOrigin || capture.siteUrl || null, baselineCapturedAt: capture.capturedAt || target.baseline.source?.exported_at || null,
      operations: target.operations.map(operation => ({ path: operation.path, label: labelFor(target, operation.path), before: operation.before, value: operation.value })),
      gallery: target.gallery ? galleryDetail(plan, target) : null,
      coverage: target.notes };
  });
  return { kind: 'update', runId: plan.runId, createdAt: plan.createdAt, jobId: plan.job.jobId, jobFile: plan.jobFile, tags: runTags(plan, targetsOf(plan)),
    supersededBy: state.supersededBy || null,
    notes: [plan.job.notes, ...plan.job.tasks.map(task => task.notes)].filter(note => typeof note === 'string' && note.trim()),
    state: state.state, reason: state.reason || null, decision: state.decision || null,
    results: state.results ? { status: state.results.status, reason: state.results.reason || null, targets: state.results.targets.map(({ key, status, errors }) => ({ key, status, errors })) } : null,
    reviewNotes: readOptional(path.join(folder, 'review-notes.json')), targets };
}

function cleanNotes(value) {
  if (value === undefined || value === null) return '';
  if (typeof value !== 'string' || value.length > 20000) throw new HttpError(400, 'Notes must be text.');
  return value.trim();
}
// approve: set the report's status line after re-checking the frozen plan.
// changesRequested: keep the report pending and record what to fix.
function decide(workspace, runId, body) {
  const folder = runFolder(workspace, runId), state = runState(workspace, runId);
  if (!['needsReview', 'changesRequested'].includes(state.state)) throw new HttpError(409, `This run is ${state.state}; it no longer accepts a review decision.`);
  if (!body || !['approve', 'changesRequested'].includes(body.decision)) throw new HttpError(400, 'Choose approve or changesRequested.');
  const notes = cleanNotes(body.notes);
  const itemNotes = Array.isArray(body.itemNotes) ? body.itemNotes.map(item => ({ key: String(item?.key || '').slice(0, 300), text: cleanNotes(item?.text) })).filter(item => item.key && item.text) : [];
  if (body.decision === 'changesRequested' && !notes && !itemNotes.length) throw new HttpError(400, 'Write what needs fixing.');
  const entry = { decision: body.decision === 'approve' ? 'approved' : 'changesRequested', decidedAt: new Date().toISOString(), notes, itemNotes };
  if (body.decision === 'approve') {
    const { reportFile } = checkPlan(workspace, runId, { approved: false });
    const text = fs.readFileSync(reportFile, 'utf8');
    const approved = text.replace(/(\r?\n)status: pending(\s*)$/, '$1status: approved$1');
    if (approved === text) throw new HttpError(409, 'Report status line is not pending.');
    fs.writeFileSync(reportFile, approved);
    checkPlan(workspace, runId);
  }
  const file = path.join(folder, 'decision.json'), previous = readOptional(file);
  const decision = { schemaVersion: '1.0', runId, ...entry, history: [...(previous?.history || []), entry] };
  store.atomicWrite(file, decision);
  return decision;
}

// The user's own edits from the hub's form: write them into the proposal and
// generate a fresh report (local only, nothing is saved to a website). Only
// text values this report already proposes can be edited; the old report
// becomes "Replaced".
function saveEdits(workspace, runId, body) {
  const folder = runFolder(workspace, runId), state = runState(workspace, runId);
  if (!['needsReview', 'changesRequested'].includes(state.state)) throw new HttpError(409, `This run is ${state.state}; it can no longer be edited.`);
  const edits = Array.isArray(body?.edits) ? body.edits : [], galleryEdits = Array.isArray(body?.gallery) ? body.gallery : [];
  if (!edits.length && !galleryEdits.length || edits.length > 100 || galleryEdits.length > 20) throw new HttpError(400, 'No edits to save.');
  const plan = store.readJson(path.join(folder, 'plan.json')), targets = targetsOf(plan), files = new Map(), log = [];
  const job = galleryEdits.length ? galleryJob(workspace, plan, targets, galleryEdits, log) : null;
  for (const edit of edits) {
    const target = targets[Number(edit?.target)], operation = target?.operations.find(op => op.path === edit?.path);
    if (!operation) throw new HttpError(400, 'Edits can only change values this report already proposes.');
    if (typeof operation.value !== 'string' || typeof edit.value !== 'string' || edit.value.length > 200000) throw new HttpError(400, 'Only text values can be edited here.');
    const input = plan.inputs.find(entry => entry.current.vehicleId === target.vehicleId);
    let entry = files.get(input.proposedFile);
    if (!entry) {
      const file = store.inside(workspace, input.proposedFile), onDisk = readOptional(file);
      if (!onDisk || !store.equal(onDisk, input.proposed)) throw new HttpError(409, 'The proposal changed after this report. Ask Claude to prepare a new one.');
      entry = { file, original: onDisk, record: store.clone(onDisk) };
      files.set(input.proposedFile, entry);
    }
    store.set(entry.record.sites[target.siteId].records[target.externalId].observed.snapshot, operation.path, edit.value);
    log.push({ target: `${target.siteId}:${target.externalId}`, path: operation.path });
  }
  for (const entry of files.values()) store.atomicWrite(entry.file, entry.record);
  if (job) store.atomicWrite(job.file, job.edited);
  let created;
  try { created = createPlan(workspace, plan.jobFile); }
  catch (error) {
    for (const entry of files.values()) store.atomicWrite(entry.file, entry.original);
    if (job) store.atomicWrite(job.file, job.original);
    throw new HttpError(400, `Could not rebuild the report: ${error.message}`);
  }
  const editedAt = new Date().toISOString(), notes = readOptional(path.join(folder, 'review-notes.json')) || {};
  store.atomicWrite(path.join(created.folder, 'review-notes.json'), { ...notes, editedFrom: runId, userEdits: [...(notes.userEdits || []), ...log.map(item => ({ ...item, editedAt }))] });
  store.atomicWrite(path.join(folder, 'superseded.json'), { schemaVersion: '1.0', runId, supersededBy: created.plan.runId, reason: 'userEdits', editedAt, edits: log });
  return { runId: created.plan.runId };
}

// Gallery edits from the hub: the user's final order of kept images and
// uploads. Images left out are removed (or not uploaded). They become the job
// target's gallery "remove" and "arrange" settings; the upload folder stays.
function galleryJob(workspace, plan, targets, galleryEdits, log) {
  const file = store.inside(workspace, plan.jobFile), original = readOptional(file);
  if (!original || !store.equal(original, plan.job)) throw new HttpError(409, 'The job changed after this report. Ask Claude to prepare a new one.');
  const edited = store.clone(original);
  for (const edit of galleryEdits) {
    const target = targets[Number(edit?.target)];
    if (!target?.gallery) throw new HttpError(400, 'This profile has no gallery change to edit.');
    const order = edit.order, baseline = target.gallery.baselineIds, names = folderFiles(plan, target).map(asset => asset.filename);
    if (!Array.isArray(order) || order.length > 500) throw new HttpError(400, 'Send the gallery as a list of images.');
    const arrange = order.map(entry => {
      if (Number.isSafeInteger(entry?.attachmentId) && baseline.includes(entry.attachmentId) && entry.filename === undefined) return { attachmentId: entry.attachmentId };
      if (typeof entry?.filename === 'string' && names.includes(entry.filename) && entry.attachmentId === undefined) return { filename: entry.filename };
      throw new HttpError(400, 'The gallery can only contain current images and images from the upload folder.');
    });
    if (new Set(arrange.map(entry => entry.filename ?? entry.attachmentId)).size !== arrange.length) throw new HttpError(400, 'An image is listed twice.');
    const selection = edited.tasks.filter(task => task.vehicleIds?.includes(target.vehicleId)).flatMap(task => task.targets || [])
      .find(entry => entry?.siteId === target.siteId && String(entry.externalId) === target.externalId && entry.gallery);
    if (!selection) throw new HttpError(409, 'The job has no gallery settings for this profile.');
    const kept = new Set(arrange.map(entry => entry.attachmentId).filter(Boolean));
    const remove = baseline.map((id, index) => kept.has(id) ? 0 : index + 1).filter(Boolean);
    selection.gallery = { ...(selection.gallery.uploadFolder !== undefined ? { uploadFolder: selection.gallery.uploadFolder } : {}),
      remove: baseline.length && remove.length === baseline.length ? 'all' : remove, arrange };
    log.push({ target: `${target.siteId}:${target.externalId}`, path: 'gallery' });
  }
  return { file, original, edited };
}

function uploadFile(workspace, runId, targetIndex, uploadIndex) {
  const folder = runFolder(workspace, runId), plan = store.readJson(path.join(folder, 'plan.json'));
  const target = targetsOf(plan)[Number(targetIndex)], asset = target?.gallery ? folderFiles(plan, target)[Number(uploadIndex)] : null;
  if (!asset) throw new HttpError(404, 'Unknown upload.');
  const file = store.inside(workspace, asset.path);
  if (!fs.existsSync(file)) throw new HttpError(404, 'Upload file is missing.');
  const bytes = fs.readFileSync(file);
  if (createHash('sha256').update(bytes).digest('hex') !== asset.sha256) throw new HttpError(409, 'Upload file changed since the report was generated.');
  return { bytes, type: TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream' };
}

function bundleDetail(workspace, file) {
  const absolute = bundlePath(workspace, file), reviewed = readOptional(reviewedPath(absolute));
  return { file: path.relative(workspace, absolute).replace(/\\/g, '/'), name: path.basename(reviewed ? reviewedPath(absolute) : absolute), bundle: reviewed || store.readJson(absolute) };
}
// The reviewed copy may differ from the bundle only in its review section.
function saveReviewedBundle(workspace, file, reviewed) {
  const absolute = bundlePath(workspace, file), original = store.readJson(absolute);
  if (!reviewed || typeof reviewed !== 'object' || Array.isArray(reviewed) || !reviewed.review || typeof reviewed.review !== 'object') throw new HttpError(400, 'Expected a reviewed bundle.');
  const { review: _a, ...rest } = reviewed, { review: _b, ...base } = original;
  if (!store.equal(rest, base)) throw new HttpError(409, 'The reviewed copy changed source data, not only the review. Reload the bundle.');
  const out = reviewedPath(absolute);
  store.atomicWrite(out, reviewed);
  return { file: path.relative(workspace, out).replace(/\\/g, '/'), status: reviewed.review.status || null };
}

// Website saves, one at a time in the order the reports were made. Each save
// is the normal command pair: catalog-update.cjs --check, then --run. Only
// approved reports (or failed ones, which the runner resumes) can be queued.
const SAVEABLE = ['readyToRun', 'failed'];
function spawnSave(workspace, runId, log) {
  const step = args => new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [path.join(workspace, 'scripts', 'catalog-update.cjs'), ...args, '--run-id', runId], { cwd: workspace, windowsHide: true });
    let buffer = '', last = '';
    const onData = chunk => { buffer += chunk; const lines = buffer.split(/\r?\n/); buffer = lines.pop(); for (const line of lines) if (line.trim()) { last = line; log(line); } };
    child.stdout.setEncoding('utf8'); child.stderr.setEncoding('utf8');
    child.stdout.on('data', onData); child.stderr.on('data', onData);
    child.on('error', reject);
    child.on('close', code => { if (buffer.trim()) { last = buffer; log(buffer); } if (code === 0) resolve(); else reject(new Error(last || `${args[0]} stopped (exit ${code}).`)); });
  });
  return step(['--check']).then(() => step(['--run']));
}
function createQueue(workspace, runner = spawnSave) {
  let items = [], active = false;
  const snapshot = () => ({ running: active, items: items.map(({ lines, ...item }) => ({ ...item, lines: lines.slice(-40) })) });
  const titleOf = runId => { const plan = store.readJson(path.join(runFolder(workspace, runId), 'plan.json')); return unique(targetsOf(plan).map(t => t.vehicleId)).map(id => vehicleLabel(plan, id)).join(', '); };
  async function work() {
    if (active) return;
    active = true;
    try {
      for (let next; (next = items.find(item => item.status === 'queued'));) {
        const log = line => { next.lines.push(line); if (next.lines.length > 400) next.lines.shift(); next.last = line; };
        next.status = 'saving'; next.startedAt = new Date().toISOString();
        try {
          const before = runState(workspace, next.runId).state;
          if (!SAVEABLE.includes(before)) throw new Error(`Not ready to save (${before}).`);
          await runner(workspace, next.runId, log);
          const after = runState(workspace, next.runId);
          next.status = after.state === 'done' ? 'done' : 'failed';
          if (next.status === 'failed') next.error = after.results?.reason || next.last || 'The save did not finish.';
        } catch (error) { next.status = 'failed'; next.error = error.message; log(error.message); }
        next.endedAt = new Date().toISOString();
        try { fs.writeFileSync(path.join(runFolder(workspace, next.runId), 'hub-save.log'), next.lines.join('\n') + '\n'); } catch { /* the log is a convenience */ }
      }
    } finally { active = false; }
  }
  function enqueue(runIds) {
    if (!Array.isArray(runIds) || !runIds.length || runIds.length > 50 || new Set(runIds).size !== runIds.length) throw new HttpError(400, 'Choose the reports to save.');
    const waiting = new Set(items.filter(item => ['queued', 'saving'].includes(item.status)).map(item => item.runId));
    const chosen = runIds.map(runId => {
      const state = runState(workspace, runId).state;
      if (waiting.has(runId)) throw new HttpError(409, `${titleOf(runId)} is already waiting to save.`);
      if (!SAVEABLE.includes(state)) throw new HttpError(409, `${titleOf(runId)} is not approved (${state}).`);
      return { runId, createdAt: store.readJson(path.join(runFolder(workspace, runId), 'plan.json')).createdAt };
    }).sort((a, b) => String(a.createdAt).localeCompare(String(b.createdAt)));
    items = [...items.filter(item => ['queued', 'saving'].includes(item.status) || Date.now() - Date.parse(item.endedAt || 0) < 6 * 3600e3).slice(-30),
      ...chosen.map(({ runId }) => ({ runId, title: titleOf(runId), status: 'queued', queuedAt: new Date().toISOString(), lines: [] }))];
    work();
    return snapshot();
  }
  function cancel(runId) {
    items = items.filter(item => item.status !== 'queued' || (runId && item.runId !== runId));
    return snapshot();
  }
  const statusOf = runId => [...items].reverse().find(item => item.runId === runId)?.status || null;
  return { enqueue, cancel, snapshot, statusOf, idle: () => new Promise(resolve => { const check = () => active ? setTimeout(check, 20) : resolve(); check(); }) };
}

function staticFile(workspace, urlPath) {
  if (urlPath === '/' || urlPath === '/index.html') return staticFile(workspace, '/hub/index.html');
  const prefix = Object.keys(STATIC).find(key => urlPath.startsWith(key));
  if (!prefix) return null;
  const name = decodeURIComponent(urlPath.slice(prefix.length));
  if (!/^[\w .-]+$/.test(name) || !TYPES[path.extname(name).toLowerCase()]) return null;
  const file = store.inside(workspace, path.join(STATIC[prefix], name));
  return fs.existsSync(file) ? { bytes: fs.readFileSync(file), type: TYPES[path.extname(name).toLowerCase()] } : null;
}

function readBody(request) {
  return new Promise((resolve, reject) => {
    let size = 0; const chunks = [];
    request.on('data', chunk => { size += chunk.length; if (size > MAX_BODY) { reject(new HttpError(413, 'Request too large.')); request.destroy(); } else chunks.push(chunk); });
    request.on('end', () => { try { resolve(JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}')); } catch { reject(new HttpError(400, 'Invalid JSON.')); } });
    request.on('error', reject);
  });
}

// Only same-machine pages can use the API: the Host header must name this
// server (blocks DNS rebinding) and writes need the per-start token, which a
// cross-origin page cannot read because no CORS headers are ever sent.
function createServer(workspace = root, { token = randomBytes(24).toString('hex'), runner } = {}) {
  const queue = createQueue(workspace, runner);
  const server = http.createServer(async (request, response) => {
    const send = (status, body, type = 'application/json; charset=utf-8') => {
      response.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer' });
      response.end(Buffer.isBuffer(body) || typeof body === 'string' ? body : JSON.stringify(body));
    };
    try {
      const port = server.address().port;
      if (![`127.0.0.1:${port}`, `localhost:${port}`].includes(request.headers.host)) throw new HttpError(403, 'Unexpected host.');
      const url = new URL(request.url, `http://127.0.0.1:${port}`), parts = url.pathname.split('/').filter(Boolean);
      if (request.method === 'POST') {
        if (request.headers['x-review-token'] !== token) throw new HttpError(403, 'Missing review token. Reload the page.');
        if (!String(request.headers['content-type'] || '').startsWith('application/json')) throw new HttpError(415, 'Send JSON.');
        const body = await readBody(request);
        if (parts[0] === 'api' && parts[1] === 'runs' && parts[3] === 'decision' && parts.length === 4) return send(200, decide(workspace, parts[2], body));
        if (parts[0] === 'api' && parts[1] === 'runs' && parts[3] === 'edits' && parts.length === 4) return send(200, saveEdits(workspace, parts[2], body));
        if (url.pathname === '/api/bundles/save') return send(200, saveReviewedBundle(workspace, body.file, body.reviewed));
        if (url.pathname === '/api/queue/save') return send(200, queue.enqueue(body.runIds));
        if (url.pathname === '/api/queue/cancel') return send(200, queue.cancel(body.runId || null));
        throw new HttpError(404, 'Not found.');
      }
      if (request.method !== 'GET') throw new HttpError(405, 'Method not allowed.');
      if (url.pathname === '/api/session') return send(200, { token });
      if (url.pathname === '/api/items') return send(200, { updates: listRuns(workspace).map(item => ({ ...item, queue: queue.statusOf(item.id) })), newVehicles: listBundles(workspace), queue: queue.snapshot() });
      if (url.pathname === '/api/queue') return send(200, queue.snapshot());
      if (parts[0] === 'api' && parts[1] === 'runs' && parts.length === 3) return send(200, runDetail(workspace, parts[2]));
      if (parts[0] === 'api' && parts[1] === 'runs' && parts[3] === 'uploads' && parts.length === 6) {
        const file = uploadFile(workspace, parts[2], parts[4], parts[5]);
        return send(200, file.bytes, file.type);
      }
      if (url.pathname === '/api/bundles') return send(200, bundleDetail(workspace, url.searchParams.get('file')));
      const file = staticFile(workspace, url.pathname);
      if (file) return send(200, file.bytes, file.type);
      throw new HttpError(404, 'Not found.');
    } catch (error) {
      send(error.status || 500, { error: error.message });
    }
  });
  return { server, token, queue };
}

function parseArgs(args) {
  const options = { port: 8787, open: true, run: null };
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--no-open') options.open = false;
    else if (args[i] === '--port' && /^\d+$/.test(args[i + 1] || '') && Number(args[i + 1]) < 65536) options.port = Number(args[++i]);
    else if (args[i] === '--run' && RUN_ID.test(args[i + 1] || '')) options.run = args[++i];
    else throw new Error('Usage: review-hub.cjs [--port 8787] [--no-open] [--run RUN-...]');
  }
  return options;
}
function main(args = process.argv.slice(2)) {
  const options = parseArgs(args), { server } = createServer(root);
  server.on('error', error => {
    if (error.code === 'EADDRINUSE') console.error(`Port ${options.port} is in use. The hub may already be open at http://127.0.0.1:${options.port}/ (or pass --port).`);
    else console.error(error.message);
    process.exitCode = 1;
  });
  server.listen(options.port, '127.0.0.1', () => {
    const url = `http://127.0.0.1:${server.address().port}/${options.run ? `?run=${options.run}` : ''}`;
    console.log(`Review hub: ${url}\nKeep this window open while reviewing. Close it (Ctrl+C) when finished.`);
    if (options.open && process.platform === 'win32') spawn('cmd', ['/c', 'start', '', url], { detached: true, stdio: 'ignore' }).unref();
  });
  return server;
}

if (require.main === module) main();
module.exports = { runState, listRuns, listBundles, runDetail, decide, saveEdits, createQueue, uploadFile, bundleDetail, saveReviewedBundle, staticFile, createServer, parseArgs };
