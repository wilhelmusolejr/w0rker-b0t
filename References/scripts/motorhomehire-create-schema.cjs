const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const store = require('./catalog-store.cjs');
const { editable } = require('./motorhomehire-search-schema.cjs');
const SITE = 'https://admin.motorhomehire.com';
const normalize = value => value.normalize('NFKD').replace(/[\u0300-\u036f]/g, '').toLowerCase().replace(/[^a-z0-9]/g, '');
function keys(object, allowed, required, label) {
  if (!object || typeof object !== 'object' || Array.isArray(object) || Object.keys(object).some(key => !allowed.includes(key)) || required.some(key => !Object.hasOwn(object, key))) throw new Error(`Missing/unknown ${label} properties.`);
}
function text(value, label, max, required = false) {
  if (typeof value !== 'string' || value.length > max || (required && !value.trim()) || value.includes('\0')) throw new Error(`Invalid ${label}; expected ${required ? 'nonempty ' : ''}text up to ${max} characters.`);
}
function initials(value) {
  const words = value.normalize('NFKD').replace(/[\u0300-\u036f]/g, '').replace(/\b\d+(?:\s*\+\s*\d+)?\s*[-–]?\s*berths?\b/gi, '').match(/[a-z]+/gi);
  if (!words?.length) throw new Error('Company/model must have letters for the provider ID.');
  return words.map(word => word[0].toUpperCase()).join('');
}
function providerId(identity) { return initials(identity.brand) + initials(identity.model) + identity.berths + 'B'; }
function validate(input) {
  keys(input, ['schemaVersion','action','siteId','requestId','vehicleId','identity','fields','insurance_table','images','exampleOnly'], ['schemaVersion','action','siteId','requestId','identity','fields','insurance_table','images'], 'creation input');
  if (input.schemaVersion !== '1.0' || input.action !== 'create' || input.siteId !== 'motorhomehireAdmin') throw new Error('Expected schemaVersion 1.0, action create and siteId motorhomehireAdmin.');
  if (input.exampleOnly) throw new Error('Complete the example and remove exampleOnly before preparing a production report.');
  if (!/^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$/.test(input.requestId || '')) throw new Error('Provide a stable requestId (letters, numbers, hyphens, underscores).');
  if (input.vehicleId != null && !store.ID.test(input.vehicleId)) throw new Error('vehicleId must be an existing permanent catalog ID or null for local resolution/allocation.');
  keys(input.identity, ['country','brand','model','variant','berths'], ['country','brand','model','berths'], 'identity');
  if (!store.COUNTRIES.includes(input.identity.country)) throw new Error('Use a known country code. Country is catalog identity, not an Add form control.');
  for (const key of ['brand','model']) text(input.identity[key], key, 100, true);
  if (input.identity.variant !== undefined) text(input.identity.variant, 'variant', 100);
  if (!Number.isSafeInteger(input.identity.berths) || input.identity.berths < 1 || input.identity.berths > 20) throw new Error('berths must be an integer from 1 to 20.');
  keys(input.fields, editable, editable, 'editable fields');
  const fields = store.clone(input.fields);
  if (fields.vehicle_id_provider === 'auto') fields.vehicle_id_provider = providerId(input.identity);
  const expectedId = providerId(input.identity);
  if (fields.vehicle_id_provider !== expectedId) throw new Error(`Provider ID must follow the selected initials rule: ${expectedId}. Use auto to generate it.`);
  for (const key of ['vehicle_id_provider','name_vehicle']) text(fields[key], key, 100, true);
  for (const key of ['vehicle_type','rental_company']) {
    keys(fields[key], ['value','text'], ['value','text'], key);
    if (!/^[1-9]\d*$/.test(fields[key].value || '')) throw new Error(`Use a selected dropdown ID for ${key}.`);
    text(fields[key].text, key + ' label', 100, true);
  }
  for (const [key,min,max,integer] of [['percentage_prepaid',0,100,false],['min_driver_age',18,100,true],['max_driver_age',18,100,true],['age_vehicle',0,100,true]]) {
    const value = fields[key];
    if (typeof value !== 'string' || !(integer ? /^\d+$/ : /^\d+(?:\.\d+)?$/).test(value) || Number(value) < min || Number(value) > max) throw new Error(`${key} must be a ${integer ? 'whole ' : ''}number string from ${min} to ${max}.`);
  }
  if (Number(fields.min_driver_age) >= Number(fields.max_driver_age)) throw new Error('The Add form requires min_driver_age to be less than max_driver_age.');
  text(fields.age_detail, 'age_detail', 255, true);
  text(fields.top_insurance, 'top_insurance', 100000);
  text(fields.terms_conditions, 'terms_conditions', 25555);
  text(fields.detail_link, 'detail_link', 2048);
  if (fields.detail_link) {
    const url = new URL(fields.detail_link);
    if (!['http:','https:'].includes(url.protocol) || url.username || url.password || url.searchParams.getAll('preview').some(v => v.toLowerCase() === 'true')) throw new Error('detail_link must be a public HTTP(S) URL without credentials or preview=true.');
  }
  keys(input.insurance_table, ['headers','rows'], ['headers','rows'], 'insurance table');
  const { headers, rows } = input.insurance_table;
  if (!Array.isArray(headers) || headers.length !== 1 || !Array.isArray(headers[0]) || headers[0].length < 1 || headers[0].length > 30 || headers[0].some(v => typeof v !== 'string') || !Array.isArray(rows) || rows.length > 100) throw new Error('Insurance table needs one header row with 1–30 columns and up to 100 body rows.');
  for (const row of rows) {
    if (!Array.isArray(row) || row.length !== headers[0].length || typeof row[0] !== 'string') throw new Error('Each insurance row must match the header width and start with a text label.');
    const checkbox = Array.isArray(row[1]);
    for (const cell of row.slice(1)) {
      if (checkbox ? !Array.isArray(cell) || cell.length !== 2 || typeof cell[0] !== 'boolean' || typeof cell[1] !== 'string' : typeof cell !== 'string') throw new Error('Use text cells or [boolean, text] cells consistently within each insurance row.');
    }
  }
  keys(input.images, ['featured','gallery'], ['featured','gallery'], 'images');
  if (input.images.featured !== null && typeof input.images.featured !== 'string' || !Array.isArray(input.images.gallery) || input.images.gallery.length > 30 || input.images.gallery.some(file => typeof file !== 'string' || !file)) throw new Error('Images need a local featured path (or null) and up to 30 local gallery paths.');
  return { ...store.clone(input), fields };
}
function imageManifest(root, images) {
  const entries = [ ...(images.featured ? [{ role:'featured', path:images.featured }] : []), ...images.gallery.map((file, position) => ({ role:'gallery', position, path:file })) ];
  const seen = new Set();
  return entries.map(entry => {
    const file = store.inside(root, entry.path), stat = fs.statSync(file);
    if (!stat.isFile() || !/\.(jpe?g|png)$/i.test(file) || stat.size < 1 || stat.size > 10 * 1024 * 1024) throw new Error('Use local JPG/PNG image files up to 10 MB.');
    const key = entry.role + ':' + file.toLowerCase();
    if (seen.has(key)) throw new Error('Duplicate gallery file.');
    seen.add(key);
    const bytes = fs.readFileSync(file);
    const png = bytes.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10]));
    const jpeg = bytes[0] === 255 && bytes[1] === 216 && bytes[2] === 255;
    if (/\.png$/i.test(file) ? !png : !jpeg) throw new Error(`Image signature does not match its extension: ${entry.path}`);
    return { ...entry, path:path.relative(root,file), size:stat.size, sha256:createHash('sha256').update(bytes).digest('hex') };
  });
}
function duplicate(snapshot, input) {
  const f = snapshot.fields;
  return f?.rental_company?.value === input.fields.rental_company.value && (normalize(f.name_vehicle || '') === normalize(input.fields.name_vehicle) || f.vehicle_id_provider?.toUpperCase() === input.fields.vehicle_id_provider);
}
function localDuplicates(root, input) {
  const matches = new Map();
  for (const entry of store.scanCurrent(root).values()) for (const external of Object.values(entry.record.sites.motorhomehireAdmin?.records || {})) {
    const snapshot = external.observed.snapshot;
    if (duplicate(snapshot,input)) matches.set(snapshot.source.vehicle_id, { externalId:snapshot.source.vehicle_id, name:snapshot.fields.name_vehicle, source:'catalog' });
  }
  for (const file of store.jsonFiles(store.inside(root,'captures/motorhomehire-search/current'))) {
    const snapshot = store.readJson(file);
    if (duplicate(snapshot,input) && !matches.has(snapshot.source.vehicle_id)) matches.set(snapshot.source.vehicle_id,{externalId:snapshot.source.vehicle_id,name:snapshot.fields.name_vehicle,source:'saved capture'});
  }
  return [...matches.values()];
}
module.exports = { SITE, normalize, initials, providerId, validate, imageManifest, duplicate, localDuplicates };
