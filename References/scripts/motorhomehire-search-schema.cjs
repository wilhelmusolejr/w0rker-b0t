const path = require('node:path');
const { isDeepStrictEqual: equal } = require('node:util');
const { createHash } = require('node:crypto');
const SITE = 'https://admin.motorhomehire.com';
const editable = ['vehicle_id_provider', 'name_vehicle', 'vehicle_type', 'rental_company', 'percentage_prepaid', 'top_insurance', 'terms_conditions', 'min_driver_age', 'max_driver_age', 'age_vehicle', 'age_detail', 'detail_link'];
const updateFields = ['age_vehicle', 'age_detail', 'detail_link'];
const system = ['_token', 'vehicle_id', 'count_image_old', 'is_main_image_id'];
function validate(d) {
  if (d.schema_version !== 1 || !/^[1-9]\d*$/.test(d.source?.vehicle_id || '') || !Number.isSafeInteger(Number(d.source.vehicle_id))) throw new Error('Invalid backend schema or vehicle ID.');
  if (d.source.url !== `${SITE}/vehicles/detail/${d.source.vehicle_id}` || d.fields?.vehicle_id !== d.source.vehicle_id) throw new Error('Backend URL/vehicle identity mismatch.');
  if (!equal(Object.keys(d).sort(), ['schema_version','source','images','fields','insurance_table'].sort())) throw new Error('Unexpected backend profile sections.');
  if (!equal(Object.keys(d.fields).sort(), [...editable, ...system].sort())) throw new Error('Missing or unknown backend fields.');
  for (const key of [...editable, ...system]) {
    const v = d.fields[key];
    if (['vehicle_type','rental_company'].includes(key)) {
      if (!v || typeof v.value !== 'string' || !/^\d+$/.test(v.value) || typeof v.text !== 'string' || !equal(Object.keys(v).sort(), ['text','value'])) throw new Error(`Invalid dropdown: ${key}`);
    } else if (typeof v !== 'string') throw new Error(`Field ${key} must remain a string.`);
  }
  for (const key of ['percentage_prepaid','min_driver_age','max_driver_age','age_vehicle']) {
    if (d.fields[key] !== '' && (!/^\d+(?:\.\d+)?$/.test(d.fields[key]) || !Number.isFinite(Number(d.fields[key])))) throw new Error(`Invalid number: ${key}`);
  }
  if (Number(d.fields.percentage_prepaid) > 100) throw new Error('Prepaid percentage exceeds 100.');
  if (d.fields.min_driver_age && d.fields.max_driver_age && Number(d.fields.min_driver_age) > Number(d.fields.max_driver_age)) throw new Error('Minimum driver age exceeds maximum.');
  if (!d.fields.name_vehicle.trim()) throw new Error('Vehicle name cannot be empty.');
  if (d.fields.detail_link) { const u = new URL(d.fields.detail_link); if (!['https:','http:'].includes(u.protocol)) throw new Error('Detail link must be HTTP or HTTPS.'); }
  if (!d.images || typeof d.images.featured !== 'string' || !Array.isArray(d.images.gallery)) throw new Error('Invalid image snapshot.');
  const t = d.insurance_table;
  if (!t || !Array.isArray(t.headers) || !Array.isArray(t.rows) || !equal(Object.keys(t).sort(), ['headers','rows'])) throw new Error('Invalid insurance table.');
  for (const row of t.headers) if (!Array.isArray(row) || row.some(c => typeof c !== 'string')) throw new Error('Invalid insurance header.');
  for (const row of t.rows) if (!Array.isArray(row) || row.some(c => !(typeof c === 'string' || (Array.isArray(c) && c.length === 2 && typeof c[0] === 'boolean' && typeof c[1] === 'string')))) throw new Error('Insurance cells must be strings or [boolean, string].');
  return d;
}
const tableShape = t => [t.headers.map(r => r.map(() => 'text')), t.rows.map(r => r.map(c => Array.isArray(c) ? 'checkbox' : 'text'))];
function changes(before, after) {
  validate(before); validate(after);
  for (const key of ['schema_version','images']) if (!equal(before[key], after[key])) throw new Error(`${key} changes are unsupported; keep the original snapshot values.`);
  const source = d => Object.fromEntries(Object.entries(d.source).filter(([k]) => k !== 'exported_at'));
  if (!equal(source(before), source(after))) throw new Error('Backend source identity cannot be changed.');
  for (const key of system.filter(k => k !== '_token')) if (before.fields[key] !== after.fields[key]) throw new Error(`System field ${key} cannot be changed.`);
  const unauthorized = editable.filter(k => !updateFields.includes(k) && !equal(before.fields[k], after.fields[k]));
  if (!equal(before.insurance_table, after.insurance_table)) unauthorized.push('insurance_table');
  if (unauthorized.length) throw new Error(`Unauthorized changes: ${unauthorized.join(', ')}. Only detail_link, age_detail, and age_vehicle may change; restore all other values from current.`);
  if (before.fields.detail_link !== after.fields.detail_link && after.fields.detail_link && new URL(after.fields.detail_link).searchParams.getAll('preview').some(value => value.toLowerCase() === 'true')) throw new Error('Preview URLs cannot be proposed as detail_link. Retain the current link and propose only other needed changes.');
  const result = updateFields.filter(k => !equal(before.fields[k], after.fields[k])).map(k => ({ field: `fields.${k}`, before: before.fields[k], after: after.fields[k] }));
  return result;
}
function fingerprint(d) {
  const copy = structuredClone(d); delete copy.fields._token; delete copy.source.exported_at;
  const stable = v => Array.isArray(v) ? v.map(stable) : v && typeof v === 'object' ? Object.fromEntries(Object.keys(v).sort().map(k => [k, stable(v[k])])) : v;
  return createHash('sha256').update(JSON.stringify(stable(copy))).digest('hex');
}
function filenameId(filename) {
  const m = /^([1-9]\d*) - .+\.json$/.exec(filename);
  if (!m || /[\\/]/.test(filename) || path.basename(filename) !== filename) throw new Error('Invalid backend profile filename.');
  return m[1];
}

module.exports = { SITE, editable, updateFields, system, validate, tableShape, changes, fingerprint, filenameId };
