const { fieldMappings, specialFeatureLabels } = require('./motorhomehire-profile-schema.cjs');
const mhEditor = require('./motorhomehire-profile-editor.cjs');
const { captureVehicleProfile } = require('./motorhomehire-profile-capture.cjs');
const cfEditor = require('./campervanfinder-editor.cjs');
const { captureProfile } = require('./campervanfinder-profile.cjs');
const backendEditor = require('./motorhomehire-search-editor.cjs');
const { saveVehicle } = require('./wordpress-save.cjs');
const store = require('./catalog-store.cjs');
const fields = require('./catalog-fields.cjs');
const gallery = require('./catalog-gallery.cjs');
const cfGallery = require('./campervanfinder-gallery.cjs');

function vehicle(target) {
  return { postId: Number(target.externalId), editUrl: target.baseline.capture?.editUrl || target.baseline.source.url };
}
async function capture(page, target, { timeout = 300000, log = console.log } = {}) {
  let snapshot;
  if (target.siteId === 'motorhomehireProfile') {
    await mhEditor.targetEditor(page, vehicle(target));
    snapshot = await page.evaluate(captureVehicleProfile, { fields: fieldMappings, specialFeatureLabels, expectedPostId: Number(target.externalId) });
  } else if (fields.isCampervanFinder(target.siteId)) {
    const country = fields.SITES[target.siteId].country;
    await cfEditor.targetEditor(page, Number(target.externalId), target.baseline.capture.editUrl, { country, timeout, log });
    snapshot = await captureProfile(page, Number(target.externalId), { country });
  } else if (target.siteId === 'motorhomehireAdmin') {
    await backendEditor.targetEditor(page, target.externalId, timeout);
    snapshot = backendEditor.snapshot(await backendEditor.readForm(page), target.baseline);
  } else throw new Error(`No capture adapter: ${target.siteId}`);
  fields.validateSnapshot(target.siteId, snapshot, target.externalId);
  return snapshot;
}
function checkPreserved(target, before, after) {
  if (target.siteId === 'motorhomehireAdmin') {
    const b = store.sanitize(before), a = store.sanitize(after);
    delete b.source.exported_at; delete a.source.exported_at;
    const allowed = new Set(target.operations.map(op => op.path));
    const unexpected = store.differences(b,a).filter(edit => !allowed.has(edit.path));
    if (unexpected.length) throw new Error(`Saved unrelated search data changed: ${unexpected.map(edit=>edit.path).join(', ')}.`);
    return;
  }
  const changed = store.differences(before.data, after.data, '/data');
  const allowed = new Set(target.operations.map(op => op.path));
  const unexpected = changed.filter(edit => !allowed.has(edit.path) && !(target.gallery && gallery.allowedChange(edit.path)));
  if (unexpected.length) throw new Error(`Saved unrelated data changed: ${unexpected.map(edit => edit.path).join(', ')}.`);
  if (fields.isCampervanFinder(target.siteId) && !store.equal(before.audit.productData, after.audit.productData)) throw new Error('Saved product commerce settings changed unexpectedly.');
}
async function applyWordPress(page, target, before, { timeout, log, onCapture, galleryContext }) {
  const camper = fields.isCampervanFinder(target.siteId);
  const resolved = [];
  for (const op of target.operations) {
    const mapping = fields.mappingFor(target.siteId, op.path, before, op.value);
    const operation = { ...mapping, before: op.before, value: op.value };
    const current = camper ? await page.evaluate(cfEditor.productOperation, { operation }) : await mhEditor.readOperation(page, operation);
    const desired = camper ? operation.type === 'checkbox' ? operation.value : (operation.value ?? '').replace(/\r\n?/g, '\n') : await mhEditor.desiredValue(page, operation);
    const same = (a, b) => camper
      ? store.equal(fields.normalized(target.siteId, op.path, a), fields.normalized(target.siteId, op.path, b))
      : store.equal(a, b);
    // Also guard controls that changed after the full capture, before editing any field.
    const original = same(current, desired) ? current : camper ? operation.type === 'checkbox' ? op.before : (op.before ?? '').replace(/\r\n?/g, '\n')
      : await mhEditor.desiredValue(page, { ...operation, value: op.before });
    if (!same(current, desired) && !same(current, original)) throw new Error(`Live control conflict: ${op.path}.`);
    resolved.push({ operation, current, desired, needsEdit: !same(current, desired) });
  }
  const edits = resolved.filter(item => item.needsEdit);
  const galleryResult = target.gallery ? await cfGallery.applyGallery(page, target, galleryContext, { timeout, log }) : { changed: false };
  if (edits.length || galleryResult.changed) {
    for (const { operation } of edits) {
      log(`Applying ${target.siteId}:${target.externalId} ${operation.jsonPath}.`);
      if (camper) await page.evaluate(cfEditor.productOperation, { operation, write: true });
      else await mhEditor.writeOperation(page, operation);
    }
    const draft = ['draft','pending'].includes(before.capture.postStatus);
    if (target.gallery) { galleryContext.journal.saveState = 'submitting'; galleryContext.persistJournal(galleryContext.journal); }
    await saveVehicle(page, Number(target.externalId), {
      siteUrl: fields.SITES[target.siteId].origin, timeout, log, retryClicks: false,
      buttonSelector: draft ? '#save-post' : '#publish', navigationWaitUntil: camper ? 'domcontentloaded' : 'load',
      confirmationSelector: '#message.updated, #message.notice-success', confirmationText: /\b(?:Product|Post)(?: draft)? updated\./i,
    });
    if (target.gallery) { galleryContext.journal.saveState = 'submitted'; galleryContext.persistJournal(galleryContext.journal); }
  }
  // Reload even for refresh-only/already-satisfied targets; never export the edited DOM.
  const after = await capture(page, target, { timeout, log });
  await onCapture(after, 'after');
  fields.checkOperations(target, after, { desired: true });
  if (target.gallery) {
    gallery.checkSnapshot(target, after, galleryContext.journal, { desired: true });
    galleryContext.journal.saveState = 'verified'; galleryContext.persistJournal(galleryContext.journal);
  }
  checkPreserved(target, before, after);
  return { status: edits.length || galleryResult.changed ? 'saved' : target.operations.length || target.gallery ? 'alreadySatisfied' : 'refreshed', snapshot: after };
}
async function processTarget(page, target, { timeout = 300000, log = console.log, onCapture, galleryContext } = {}) {
  const before = await capture(page, target, { timeout, log });
  await onCapture(before, 'before');
  fields.checkOperations(target, before);
  if (target.gallery) gallery.checkSnapshot(target, before, galleryContext?.journal);
  if (target.siteId !== 'motorhomehireAdmin') return applyWordPress(page, target, before, { timeout, log, onCapture, galleryContext });
  const baseline = fields.backendSnapshot(before), proposed = store.clone(baseline);
  for (const operation of target.operations) store.set(proposed, operation.path, operation.value);
  const result = await backendEditor.processVehicle(page, {
    postId: Number(target.externalId), filename: `${target.externalId}.json`, baseline, proposed,
    operations: target.operations.map(op => ({ field: op.path.slice(1).replace(/\//g, '.'), before: store.get(baseline, op.path), after: op.value })),
  }, { timeout, async store(snapshot) { await onCapture(snapshot, 'after'); fields.checkOperations(target, snapshot, { desired: true }); } });
  return { status: result.status === 'saved' ? 'saved' : target.operations.length ? 'alreadySatisfied' : 'refreshed' };
}

module.exports = { capture, checkPreserved, applyWordPress, processTarget };
