const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const store = require('./catalog-store.cjs');

const GALLERY_PATH = '/data/descriptionAndImages/image_gallery';
const SITE_IDS = ['campervanfinderAuProfile', 'campervanfinderNzProfile'];
const TYPES = { '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.png': 'image/png', '.gif': 'image/gif', '.webp': 'image/webp' };
const IMAGE_EXTENSION = /\.(?:jpg|jpeg|png|gif|webp|svg|avif|heic|heif|bmp|tif|tiff|ico)$/i;
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const uploadKey = asset => store.hash({ path: asset.path, sha256: asset.sha256 });

function validateRequest(request) {
  if (!request || typeof request !== 'object' || Array.isArray(request) || Object.keys(request).some(key => !['uploadFolder', 'remove', 'order', 'arrange'].includes(key))) throw new Error('Unknown/invalid gallery settings. Use uploadFolder, remove and optional order or arrange.');
  if (request.uploadFolder !== undefined && (typeof request.uploadFolder !== 'string' || !request.uploadFolder.trim())) throw new Error('gallery.uploadFolder must be a nonempty folder path.');
  if (request.uploadFolder === undefined && request.remove === undefined && request.arrange === undefined) throw new Error('Gallery settings need uploadFolder, an explicit remove instruction or arrange.');
  if (request.remove !== undefined && request.remove !== 'all' && (!Array.isArray(request.remove) || new Set(request.remove).size !== request.remove.length || request.remove.some(position => !Number.isSafeInteger(position) || position < 1))) throw new Error('gallery.remove must be "all" or a unique array of positions starting at 1; [] keeps existing images.');
  if (request.order !== undefined) {
    const order = request.order;
    if (!request.uploadFolder || !order || typeof order !== 'object' || Array.isArray(order) || !store.equal(Object.keys(order).sort(), ['file', 'group']) || [order.file, order.group].some(value => typeof value !== 'string' || !value.trim())) throw new Error('gallery.order requires uploadFolder and {file, group}.');
  }
  if (request.arrange !== undefined) {
    if (request.order !== undefined) throw new Error('Use gallery.order or gallery.arrange, not both.');
    const entries = request.arrange;
    if (!Array.isArray(entries) || entries.some(entry => !entry || typeof entry !== 'object' || Array.isArray(entry) || Object.keys(entry).length !== 1
      || (Object.hasOwn(entry, 'attachmentId') ? !Number.isSafeInteger(entry.attachmentId) || entry.attachmentId < 1 : typeof entry.filename !== 'string' || !entry.filename))) throw new Error('gallery.arrange must list {attachmentId} or {filename} entries in final order.');
    const keys = entries.map(entry => entry.filename ?? entry.attachmentId);
    if (new Set(keys).size !== keys.length) throw new Error('gallery.arrange lists an image twice.');
  }
  return request;
}

function imageAsset(root, file) {
  const checked = store.inside(root, file), stat = fs.statSync(checked);
  const ext = path.extname(checked).toLowerCase(), mimeType = TYPES[ext];
  if (!mimeType || !stat.isFile() || stat.size < 16 || stat.size > 200 * 1024 * 1024) throw new Error(`Unsupported/empty/oversized gallery image: ${file}`);
  const bytes = fs.readFileSync(checked);
  const valid = mimeType === 'image/jpeg' ? bytes[0] === 0xff && bytes[1] === 0xd8 && bytes[2] === 0xff
    : mimeType === 'image/png' ? bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))
    : mimeType === 'image/gif' ? /^GIF8[79]a$/.test(bytes.toString('ascii', 0, 6))
    : bytes.toString('ascii', 0, 4) === 'RIFF' && bytes.toString('ascii', 8, 12) === 'WEBP';
  if (!valid) throw new Error(`Image signature does not match its extension: ${file}`);
  return { filename: path.basename(checked), path: path.relative(root, checked), size: bytes.length, mimeType, sha256: digest(bytes) };
}

// Filesystem reads happen when making/checking a report, not when deriving its
// frozen operations. Older runs without galleries retain their original shape.
function prepareAssets(root, request) {
  validateRequest(request);
  const assets = { uploadFolder: null, files: [], orderSource: null };
  if (request.uploadFolder !== undefined) {
    const folder = store.inside(root, request.uploadFolder);
    if (!fs.existsSync(folder) || !fs.statSync(folder).isDirectory()) throw new Error('Gallery upload folder is missing or is not a directory.');
    assets.uploadFolder = path.relative(root, folder);
    for (const entry of fs.readdirSync(folder, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name, 'en', { numeric: true }) || a.name.localeCompare(b.name, 'en'))) {
      if (!IMAGE_EXTENSION.test(entry.name) || entry.isDirectory()) continue;
      assets.files.push(imageAsset(root, store.inside(folder, entry.name)));
    }
    if (!assets.files.length) throw new Error('Gallery upload folder has no supported images; nothing will be removed.');
    if (new Set(assets.files.map(asset => asset.filename.toLowerCase())).size !== assets.files.length) throw new Error('Gallery filenames must be unique ignoring case.');
  }
  if (request.order) {
    const file = store.inside(root, request.order.file), data = store.readJson(file);
    if (!Object.hasOwn(data, request.order.group) || !Array.isArray(data[request.order.group])) throw new Error(`Gallery order group is missing: ${request.order.group}`);
    assets.orderSource = { file: path.relative(root, file), group: request.order.group, sha256: digest(fs.readFileSync(file)), entries: data[request.order.group] };
  }
  orderedFiles(assets);
  return assets;
}

function orderedFiles(assets) {
  if (!assets || !Array.isArray(assets.files)) throw new Error('Missing frozen gallery assets.');
  if (!assets.orderSource) return store.clone(assets.files);
  const entries = assets.orderSource.entries;
  if (!Array.isArray(entries) || entries.length !== assets.files.length || entries.some(entry => !entry || !store.equal(Object.keys(entry).sort(), ['filename', 'order']) || !Number.isSafeInteger(entry.order) || entry.order < 1 || typeof entry.filename !== 'string')) throw new Error('Gallery order must list every upload filename exactly once with a positive integer order.');
  const sorted = [...entries].sort((a, b) => a.order - b.order);
  if (new Set(sorted.map(entry => entry.order)).size !== sorted.length || new Set(sorted.map(entry => entry.filename)).size !== sorted.length) throw new Error('Gallery order contains duplicate positions or filenames.');
  const files = new Map(assets.files.map(asset => [asset.filename, asset]));
  return sorted.map(entry => {
    if (!files.has(entry.filename)) throw new Error(`Gallery order filename is not in the upload folder: ${entry.filename}`);
    return store.clone(files.get(entry.filename));
  });
}

function ids(snapshot) {
  const gallery = store.get(snapshot, GALLERY_PATH);
  if (!gallery || !Array.isArray(gallery.images) || gallery.image_count !== gallery.images.length || gallery.images.some((image, index) => !Number.isSafeInteger(image.attachmentId) || image.attachmentId < 1 || image.position !== index + 1)) throw new Error('Incomplete or invalid gallery capture.');
  const result = gallery.images.map(image => image.attachmentId);
  if (new Set(result).size !== result.length) throw new Error('Duplicate gallery attachment IDs.');
  return result;
}

function resolve(request, assets, baseline) {
  validateRequest(request);
  const baselineIds = ids(baseline), uploads = orderedFiles(assets);
  const removal = request.remove ?? (request.uploadFolder !== undefined ? 'all' : []);
  const positions = removal === 'all' ? baselineIds.map((_, index) => index + 1) : [...removal].sort((a, b) => a - b);
  if (positions.some(position => position > baselineIds.length)) throw new Error(`Gallery removal position is outside the ${baselineIds.length}-image baseline.`);
  const remove = positions.map(position => ({ position, attachmentId: baselineIds[position - 1] }));
  const deleted = new Set(remove.map(image => image.attachmentId)), retainedIds = baselineIds.filter(id => !deleted.has(id));
  if (!request.arrange) return { baselineIds, remove, retainedIds, uploads, uploadFolder: assets.uploadFolder, orderSource: store.clone(assets.orderSource) };
  const kept = request.arrange.filter(entry => Object.hasOwn(entry, 'attachmentId')).map(entry => entry.attachmentId);
  if (kept.length !== retainedIds.length || kept.some(id => !retainedIds.includes(id))) throw new Error('gallery.arrange must list every kept image (and no removed image) by attachmentId.');
  const files = new Map(uploads.map(asset => [asset.filename, asset])), chosen = [];
  for (const entry of request.arrange) if (Object.hasOwn(entry, 'filename')) {
    if (!files.has(entry.filename)) throw new Error(`gallery.arrange filename is not in the upload folder: ${entry.filename}`);
    chosen.push(store.clone(files.get(entry.filename)));
  }
  return { baselineIds, remove, retainedIds, uploads: chosen, skippedUploads: uploads.filter(asset => !chosen.some(item => item.filename === asset.filename)).map(asset => asset.filename),
    finalOrder: store.clone(request.arrange), uploadFolder: assets.uploadFolder, orderSource: null };
}

function prepareInputs(root, task) {
  const selections = Array.isArray(task.targets) ? task.targets.filter(target => target && Object.hasOwn(target, 'gallery')) : [];
  if (!selections.length) return [];
  if (task.vehicleIds?.length !== 1) throw new Error('Gallery tasks require exactly one explicit vehicleId. Use one task per vehicle.');
  return selections.map(selection => {
    if (!SITE_IDS.includes(selection.siteId) || !/^[1-9]\d*$/.test(String(selection.externalId || ''))) throw new Error('Gallery updates require an explicit Campervan Finder AU/NZ profile ID.');
    return { siteId: selection.siteId, externalId: String(selection.externalId), assets: prepareAssets(root, selection.gallery) };
  });
}

function checkInputs(root, plan) {
  for (const input of plan.inputs) for (const frozen of input.galleryInputs || []) {
    const task = plan.job.tasks.find(task => task.taskId === input.taskId);
    const request = task.targets.find(target => target.siteId === frozen.siteId && String(target.externalId) === frozen.externalId)?.gallery;
    if (!store.equal(prepareAssets(root, request), frozen.assets)) throw new Error('Gallery files, folder membership or order file changed since review. Generate another report.');
  }
}

function journalFor(target) {
  return { schemaVersion: '1.0', galleryHash: store.hash(target.gallery), uploads: [], saveState: 'notSubmitted' };
}
function validateJournal(target, journal) {
  if (!journal || journal.schemaVersion !== '1.0' || journal.galleryHash !== store.hash(target.gallery) || !Array.isArray(journal.uploads) || !['notSubmitted', 'submitting', 'submitted', 'verified'].includes(journal.saveState)) throw new Error('Gallery recovery journal does not match the frozen target.');
  const assets = new Map(target.gallery.uploads.map(asset => [uploadKey(asset), asset])), seen = new Set();
  for (const entry of journal.uploads) {
    if (!entry || !assets.has(entry.key) || seen.has(entry.key) || !['submitting', 'uploaded', 'failed'].includes(entry.status) || !Array.isArray(entry.existingIds) || entry.existingIds.some(id => !Number.isSafeInteger(id) || id < 1) || entry.status === 'uploaded' && (!Number.isSafeInteger(entry.attachmentId) || entry.attachmentId < 1 || entry.existingIds.includes(entry.attachmentId))) throw new Error('Invalid gallery upload journal entry.');
    seen.add(entry.key);
  }
  return journal;
}
function desiredIds(target, journal) {
  validateJournal(target, journal);
  const uploads = target.gallery.uploads.map(asset => journal.uploads.find(entry => entry.key === uploadKey(asset)));
  if (uploads.some(entry => entry?.status !== 'uploaded')) return null;
  const uploaded = new Map(target.gallery.uploads.map((asset, index) => [asset.filename, uploads[index].attachmentId]));
  const result = target.gallery.finalOrder ? target.gallery.finalOrder.map(entry => entry.attachmentId ?? uploaded.get(entry.filename))
    : [...target.gallery.retainedIds, ...uploads.map(entry => entry.attachmentId)];
  if (new Set(result).size !== result.length) throw new Error('Gallery recovery has duplicate attachment IDs.');
  return result;
}
function checkSnapshot(target, snapshot, journal, { desired = false } = {}) {
  if (!target.gallery) return;
  const actual = ids(snapshot), expected = desiredIds(target, journal);
  if (expected && store.equal(actual, expected)) return;
  if (!desired && store.equal(actual, target.gallery.baselineIds)) return;
  throw new Error(`${desired ? 'Saved gallery mismatch' : 'Live gallery conflict'}: attachment IDs/order differ from the reviewed ${desired ? 'result' : 'baseline'}.`);
}
function allowedChange(pointer) { return pointer === `${GALLERY_PATH}/images` || pointer === `${GALLERY_PATH}/image_count`; }
function reportLines(gallery) {
  return [
    `  GALLERY: ${gallery.baselineIds.length} existing -> ${gallery.retainedIds.length + gallery.uploads.length} images`,
    `    Existing IDs in order: ${JSON.stringify(gallery.baselineIds)}`,
    `    Remove original positions/IDs: ${JSON.stringify(gallery.remove)}`,
    `    Retain IDs in order: ${JSON.stringify(gallery.retainedIds)}`,
    `    Upload folder: ${gallery.uploadFolder ?? '(no uploads)'}`,
    ...gallery.uploads.map((asset, index) => `    Upload ${index + 1}: ${asset.filename} (${asset.size} bytes; SHA-256 ${asset.sha256})`),
    ...(gallery.skippedUploads?.length ? [`    Not uploaded (left out of arrange): ${JSON.stringify(gallery.skippedUploads)}`] : []),
    `    Final order: ${JSON.stringify(gallery.finalOrder || [...gallery.retainedIds.map(attachmentId => ({ attachmentId })), ...gallery.uploads.map(asset => ({ filename: asset.filename }))])}`,
    `    Ordering: ${gallery.finalOrder ? 'arrange (full final order)' : gallery.orderSource ? `${gallery.orderSource.file} / ${gallery.orderSource.group}` : 'filename order; retained images first'}`,
    '    Removal detaches gallery entries. Featured image and Media Library files are preserved.',
  ];
}

module.exports = { GALLERY_PATH, SITE_IDS, TYPES, digest, uploadKey, validateRequest, imageAsset, prepareAssets, orderedFiles, ids, resolve, prepareInputs, checkInputs, journalFor, validateJournal, desiredIds, checkSnapshot, allowedChange, reportLines };
