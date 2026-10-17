const fs = require('node:fs');
const path = require('node:path');
const { createHash, randomUUID } = require('node:crypto');
const { isDeepStrictEqual: equal } = require('node:util');

const SCHEMA = '1.0';
const ID = /^veh_\d{6,}$/;
const COUNTRIES = ['au', 'nz', 'ca', 'us'];
const forbidden = new Set(['__proto__', 'prototype', 'constructor']);
const stable = v => Array.isArray(v) ? v.map(stable) : v && typeof v === 'object' ? Object.fromEntries(Object.keys(v).sort().map(k => [k, stable(v[k])])) : v;
const hash = v => createHash('sha256').update(JSON.stringify(stable(v))).digest('hex');
const clone = v => structuredClone(v);

// JSON.parse accepts duplicate keys. Reject them before accepting job/baseline intent.
function parseJson(text) {
  text = text.replace(/^\uFEFF/, '');
  const tokens = text.match(/"(?:[^"\\]|\\[\s\S])*"|[{}\[\],:]|[^\s{}\[\],:]+/g) || [];
  let i = 0;
  function value() {
    if (tokens[i] === '{') {
      i++; const keys = new Set();
      if (tokens[i] !== '}') do {
        const key = JSON.parse(tokens[i++]);
        if (typeof key !== 'string' || keys.has(key) || forbidden.has(key)) throw new Error(`Duplicate or unsafe JSON key: ${key}`);
        keys.add(key);
        if (tokens[i++] !== ':') throw new Error('Invalid JSON object.');
        value();
        if (tokens[i] !== ',') break;
        i++;
      } while (true);
      if (tokens[i++] !== '}') throw new Error('Invalid JSON object ending.');
    } else if (tokens[i] === '[') {
      i++;
      if (tokens[i] !== ']') do { value(); if (tokens[i] !== ',') break; i++; } while (true);
      if (tokens[i++] !== ']') throw new Error('Invalid JSON array ending.');
    } else JSON.parse(tokens[i++]);
  }
  value();
  if (i !== tokens.length) throw new Error('Unexpected JSON content.');
  return JSON.parse(text);
}

function inside(root, relative) {
  const base = path.resolve(root), resolved = path.resolve(base, relative);
  const rel = path.relative(base, resolved);
  if (rel === '..' || rel.startsWith(`..${path.sep}`) || path.isAbsolute(rel)) throw new Error(`Path is outside ${base}: ${relative}`);
  // Reject junctions/symlinks as well as lexical traversal.
  let cursor = base;
  for (const segment of ['', ...rel.split(path.sep).filter(Boolean)]) {
    if (segment) cursor = path.join(cursor, segment);
    if (fs.existsSync(cursor) && fs.lstatSync(cursor).isSymbolicLink()) throw new Error(`Symbolic link/junction is unsupported: ${cursor}`);
  }
  return resolved;
}
const readJson = file => parseJson(fs.readFileSync(file, 'utf8'));
function atomicWrite(file, data) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const temp = `${file}.${randomUUID()}.tmp`;
  try {
    const fd = fs.openSync(temp, 'wx');
    try { fs.writeFileSync(fd, typeof data === 'string' ? data : JSON.stringify(data, null, 2) + '\n', 'utf8'); fs.fsyncSync(fd); }
    finally { fs.closeSync(fd); }
    fs.renameSync(temp, file);
  } finally { if (fs.existsSync(temp)) fs.unlinkSync(temp); }
}
function jsonFiles(folder) {
  if (!fs.existsSync(folder)) return [];
  return fs.readdirSync(folder, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name)).flatMap(entry => {
    const file = inside(folder, entry.name);
    if (entry.isSymbolicLink()) throw new Error(`Unexpected symbolic link: ${file}`);
    return entry.isDirectory() ? jsonFiles(file) : entry.isFile() && entry.name.endsWith('.json') ? [file] : [];
  });
}
function pointerParts(pointer) {
  if (!pointer.startsWith('/') || /~(?![01])/u.test(pointer)) throw new Error(`Invalid JSON pointer: ${pointer}`);
  const keys = pointer.slice(1).split('/').map(k => k.replace(/~1/g, '/').replace(/~0/g, '~'));
  if (keys.some(k => forbidden.has(k))) throw new Error('Unsafe JSON pointer.');
  return keys;
}
function get(object, pointer) {
  let v = object;
  for (const key of pointerParts(pointer)) {
    if (!v || typeof v !== 'object' || !Object.hasOwn(v, key)) return undefined;
    v = v[key];
  }
  return v;
}
function set(object, pointer, value) {
  const keys = pointerParts(pointer); let parent = object;
  for (const key of keys.slice(0, -1)) {
    if (!parent || typeof parent !== 'object' || !Object.hasOwn(parent, key)) throw new Error(`Missing parent: ${pointer}`);
    parent = parent[key];
  }
  if (!parent || typeof parent !== 'object' || !Object.hasOwn(parent, keys.at(-1))) throw new Error(`Missing destination: ${pointer}`);
  parent[keys.at(-1)] = clone(value);
}
function differences(before, after, pointer = '') {
  if (equal(before, after)) return [];
  if (before && after && typeof before === 'object' && typeof after === 'object' && !Array.isArray(before) && !Array.isArray(after)) {
    return [...new Set([...Object.keys(before), ...Object.keys(after)])].sort().flatMap(key => differences(before[key], after[key], `${pointer}/${key.replace(/~/g, '~0').replace(/\//g, '~1')}`));
  }
  return [{ path: pointer, before, value: after }];
}
function sanitize(snapshot) {
  if (Array.isArray(snapshot)) return snapshot.map(sanitize);
  if (snapshot && typeof snapshot === 'object') return Object.fromEntries(Object.entries(snapshot).filter(([key]) => !['_token', 'csrfToken', 'nonce'].includes(key)).map(([key, value]) => [key, sanitize(value)]));
  return snapshot;
}
function contentProjection(snapshot) {
  if (snapshot.schema_version === 1) {
    const copy = sanitize(snapshot); delete copy.source.exported_at; return copy;
  }
  const { postId, postType, siteUrl, editUrl, postStatus, slug, publicUrl } = snapshot.capture;
  return { schemaVersion: snapshot.schemaVersion, bundleType: snapshot.bundleType, capture: { postId, postType, siteUrl, editUrl, postStatus, slug, publicUrl }, data: snapshot.data };
}
function observation(snapshot, extra = {}) {
  const clean = sanitize(snapshot);
  return { ...extra, capturedAt: clean.capture?.capturedAt || clean.source?.exported_at || null,
    captureStatus: 'captured', captureHash: hash(clean), contentHash: hash(contentProjection(clean)),
    hashProjection: 'catalogContent.v1', snapshot: clean };
}
function filename(record) {
  const label = s => s.trim().replace(/\s+/g, ' ').replace(/[<>:"/\\|?*\x00-\x1f]/g, '-').replace(/[. ]+$/g, '');
  let brand = label(record.identity.brand), model = label(record.identity.model);
  if (!brand || !model) throw new Error('Brand/model must produce nonempty filename labels.');
  const prefix = `${record.vehicleId} - ${record.identity.country} - `;
  while (prefix.length + brand.length + model.length + 8 > 180) {
    if (model.length > 1) model = model.slice(0, -1).replace(/[. ]+$/g, '');
    else if (brand.length > 1) brand = brand.slice(0, -1).replace(/[. ]+$/g, '');
    else throw new Error('Vehicle ID leaves insufficient filename space.');
  }
  return `${prefix}${brand} - ${model}.json`;
}
function validateRecord(record, { proposed = false } = {}) {
  if (record?.schemaVersion !== SCHEMA || record.recordType !== 'vehicleCatalogRecord' || !ID.test(record.vehicleId || '')) throw new Error('Expected a catalog schema 1.0 vehicle record with a permanent veh_ ID.');
  if (!Number.isSafeInteger(record.catalogRevision) || record.catalogRevision < 1 || !COUNTRIES.includes(record.identity?.country)) throw new Error('Invalid catalog revision/country.');
  for (const key of ['brand', 'model']) if (typeof record.identity[key] !== 'string' || !record.identity[key].trim()) throw new Error(`Missing canonical ${key}.`);
  if (!record.sharedFacts || !record.sites || typeof record.sites !== 'object' || Array.isArray(record.sites)) throw new Error('Missing sharedFacts/sites.');
  for (const site of Object.values(record.sites)) {
    if (!site?.records || typeof site.records !== 'object' || Array.isArray(site.records)) throw new Error('Expected site records keyed by external ID.');
    for (const [id, external] of Object.entries(site.records)) {
      if (!/^[1-9]\d*$/.test(id) || !Number.isSafeInteger(Number(id)) || !external?.mapping || !external.observed?.snapshot) throw new Error('Invalid external record/observation.');
      if (!proposed && (!equal(external.observed.snapshot, sanitize(external.observed.snapshot)) || external.observed.captureHash !== hash(external.observed.snapshot) || external.observed.contentHash !== hash(contentProjection(external.observed.snapshot)))) throw new Error(`Invalid stored capture hashes or session values for ${id}.`);
    }
  }
  filename(record);
  return record;
}
function scanCurrent(root) {
  const records = new Map(), links = new Map();
  for (const file of jsonFiles(inside(root, 'catalog/current'))) {
    const record = validateRecord(readJson(file));
    if (records.has(record.vehicleId)) throw new Error(`Duplicate current vehicle ID: ${record.vehicleId}`);
    if (path.basename(file) !== filename(record) || path.basename(path.dirname(file)) !== record.identity.country) throw new Error(`Current filename/country disagrees with ${record.vehicleId}.`);
    records.set(record.vehicleId, { file, record });
    for (const [siteId, site] of Object.entries(record.sites)) for (const id of Object.keys(site.records)) {
      const key = `${siteId}:${id}`;
      if (links.has(key)) throw new Error(`External record maps to multiple vehicles: ${key}`);
      links.set(key, record.vehicleId);
    }
  }
  return records;
}
function withLock(root, key, work) {
  if (!/^[a-zA-Z0-9_-]+$/.test(key)) throw new Error('Invalid lock key.');
  const file = inside(root, `catalog/.locks/${key}.lock`); fs.mkdirSync(path.dirname(file), { recursive: true });
  let fd;
  try { fd = fs.openSync(file, 'wx'); }
  catch (error) { if (error.code === 'EEXIST') throw new Error(`Catalog lock is held: ${key}. Recover a stale lock before retrying.`); throw error; }
  try { fs.writeFileSync(fd, JSON.stringify({ pid: process.pid, createdAt: new Date().toISOString() })); return work(); }
  finally { fs.closeSync(fd); fs.unlinkSync(file); }
}
async function withAsyncLock(root, key, work) {
  if (!/^[a-zA-Z0-9_-]+$/.test(key)) throw new Error('Invalid lock key.');
  const file = inside(root, `catalog/.locks/${key}.lock`); fs.mkdirSync(path.dirname(file), { recursive: true });
  let fd;
  try { fd = fs.openSync(file, 'wx'); }
  catch (error) { if (error.code === 'EEXIST') throw new Error(`Catalog lock is held: ${key}. Another run may be editing this record.`); throw error; }
  try { fs.writeFileSync(fd, JSON.stringify({ pid: process.pid, createdAt: new Date().toISOString() })); return await work(); }
  finally { fs.closeSync(fd); fs.unlinkSync(file); }
}
function findCurrent(root, vehicleId) {
  if (!ID.test(vehicleId)) throw new Error('Invalid internal vehicle ID.');
  const files = jsonFiles(inside(root, 'catalog/current')).filter(file => path.basename(file).startsWith(`${vehicleId} - `));
  if (files.length !== 1) throw new Error(`Expected exactly one current file for ${vehicleId}.`);
  const record = validateRecord(readJson(files[0]));
  if (record.vehicleId !== vehicleId || path.basename(files[0]) !== filename(record)) throw new Error('Current filename and identity disagree.');
  return { file: files[0], record };
}
function persist(root, record, previousFile) {
  validateRecord(record);
  const destination = inside(root, `catalog/current/${record.identity.country}/${filename(record)}`);
  if (previousFile && path.resolve(previousFile) !== destination) throw new Error('Canonical identity/filename changes need an explicit migration; update jobs preserve identity.');
  if (fs.existsSync(destination) && readJson(destination).vehicleId !== record.vehicleId) throw new Error('Catalog destination belongs to another vehicle.');
  atomicWrite(destination, record); return destination;
}
function incorporate(root, target, snapshot, { reconcile = r => r, phase = 'after' } = {}) {
  require('./catalog-fields.cjs').validateSnapshot(target.siteId, snapshot, target.externalId);
  return withLock(root, target.vehicleId, () => {
    const entry = findCurrent(root, target.vehicleId);
    const record = clone(entry.record), external = record.sites[target.siteId]?.records[target.externalId];
    if (!external || !equal(external.mapping, target.mapping)) throw new Error('Website mapping changed during the run.');
    external.observed = observation(snapshot);
    external.lastReadAttempt = { at: new Date().toISOString(), status: 'success', phase };
    record.catalogRevision++;
    const updated = reconcile(record);
    return { file: persist(root, updated, entry.file), record: updated };
  });
}
function recordReadFailure(root, target, error) {
  withLock(root, target.vehicleId, () => {
    const entry = findCurrent(root, target.vehicleId);
    const record = clone(entry.record), external = record.sites[target.siteId]?.records[target.externalId];
    if (!external) throw new Error('Current mapping disappeared.');
    external.lastReadAttempt = { at: new Date().toISOString(), status: 'failed', reason: error.message };
    record.catalogRevision++; persist(root, record, entry.file);
  });
}

module.exports = { SCHEMA, ID, COUNTRIES, stable, hash, clone, equal, parseJson, readJson, inside, atomicWrite, jsonFiles, get, set, differences, sanitize, contentProjection, observation, filename, validateRecord, scanCurrent, findCurrent, withLock, withAsyncLock, persist, incorporate, recordReadFailure };
