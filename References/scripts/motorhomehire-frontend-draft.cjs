// Builds and checks local drafts for new Motorhome Hire public vehicle profiles.
// Offline only: reads the catalog and saved captures, never opens a website.
const fs = require('node:fs');
const path = require('node:path');
const store = require('./catalog-store.cjs');
const schema = require('./motorhomehire-frontend-schema.cjs');
const { root, captureFolder } = require('./project.cjs');

const DRAFTS = 'jobs/frontend-drafts';
// Supplier-wide terms; same-country profiles of the brand prefill them only when they all agree.
const companyPaths = ['data.importantInformation.minimum_driver_age','data.importantInformation.driving_license','data.importantInformation.pets_allowed',
  'data.standardInclusions.kilometres_included','data.standardInclusions.bedding__linen','data.standardInclusions.kitchen_equipment__cutlery','data.standardInclusions.roadside_assistance',
  'data.standardInclusions.tourist_info_maps','data.standardInclusions.one_way_fee','data.standardInclusions.airport_transfer','data.taxonomies.rental_locations'];

function berthsFor(identity) {
  for (const text of [identity.variant, identity.model]) {
    const match = /(\d+)\s*[- ]?\s*berths?\b/i.exec(text || '');
    if (match) return Number(match[1]);
  }
  return null;
}
function frontendSnapshots(record) {
  return Object.entries(record.sites.motorhomehireProfile?.records || {}).map(([postId, external]) => ({ postId, snapshot: external.observed.snapshot }));
}
// Prefer the term already used for this catalog brand on linked profiles, then an exact name match.
function brandTerm(current, identity) {
  const used = new Set();
  for (const { record } of current.values()) if (schema.normalize(record.identity.brand) === schema.normalize(identity.brand))
    for (const { snapshot } of frontendSnapshots(record)) if (snapshot.data.taxonomies?.brand) used.add(snapshot.data.taxonomies.brand);
  if (used.size === 1) return { term: [...used][0], note: 'Brand term used by this brand\'s linked Motorhome Hire profiles.' };
  if (used.size > 1) return { term: null, candidates: [...used] };
  const exact = schema.brandTerms.find(term => schema.normalize(term) === schema.normalize(identity.brand));
  return exact ? { term: exact, note: 'Brand term with the same name as the catalog brand.' } : { term: null, candidates: [] };
}
function siblings(current, record) {
  return [...current.values()].filter(({ record: r }) => r.vehicleId !== record.vehicleId && r.identity.country === record.identity.country && schema.normalize(r.identity.brand) === schema.normalize(record.identity.brand))
    .flatMap(({ record: r }) => frontendSnapshots(r).filter(({ snapshot }) => snapshot.capture.postStatus === 'publish').map(item => ({ ...item, vehicleId: r.vehicleId, name: item.snapshot.data.vehicle.name })));
}
function htmlText(value) {
  return String(value ?? '').replace(/<br\s*\/?>/gi, '\n').replace(/<\/(p|tr|li|h\d)>/gi, '\n').replace(/<\/th>/gi, ':\n').replace(/<[^>]+>/g, '')
    .replace(/&nbsp;/g, ' ').replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#0?39;/g, "'")
    .split('\n').map(line => line.trim()).filter(Boolean).join('\n');
}
const shown = value => value === null || value === undefined ? '(unknown)' : JSON.stringify(value);

function buildDraft(workspace, vehicleId) {
  const current = store.scanCurrent(workspace), entry = current.get(vehicleId);
  if (!entry) throw new Error(`No current catalog vehicle ${vehicleId}. Identify the vehicle locally first.`);
  const record = entry.record;
  if (Object.keys(record.sites.motorhomehireProfile?.records || {}).length) throw new Error(`${vehicleId} already has a linked Motorhome Hire profile. Use the update workflow.`);
  if (!schema.countryRoot[record.identity.country]) throw new Error(`Motorhome Hire has no location root for country ${record.identity.country}.`);
  const data = schema.blankData(), sources = {}, berths = berthsFor(record.identity);
  const put = (jsonPath, value, source) => { store.set({ data }, schema.pointer(jsonPath), value); sources[jsonPath] = [source]; };

  const brand = brandTerm(current, record.identity);
  if (brand.term) put('data.taxonomies.brand', brand.term, { type: 'derived', note: brand.note });
  const label = brand.term || record.identity.brand;
  let name = schema.normalize(record.identity.model).startsWith(schema.normalize(label)) ? record.identity.model : `${label} ${record.identity.model}`;
  if (berths && !/berth/i.test(name)) name += ` - ${berths} Berth`;
  put('data.vehicle.name', name, { type: 'derived', note: 'Brand term + catalog model + berth count. Adjust to the supplier\'s exact vehicle title.' });
  if (berths >= 2 && berths <= 6) for (const n of [2,3,4,5,6]) put(`data.taxonomies.special_features.${n}_berth`, n === berths, { type: 'derived', note: `Catalog identity is ${berths} berth.` });

  const age = record.sharedFacts.vehicleAge?.value;
  if (age) {
    const text = (age.min === age.max ? String(age.min) : `${age.min}-${age.max}`) + (age.kind === 'years' ? (age.max === 1 ? ' year' : ' years') : '');
    for (const jsonPath of ['data.vehicleSpecifications.vehicle_age','data.featuresIcons.vehicle_ages']) put(jsonPath, text, { type: 'sharedFact', fact: 'vehicleAge' });
  }
  const driverAge = record.sharedFacts.minimumDriverAge?.value;
  if (Number.isInteger(driverAge)) put('data.importantInformation.minimum_driver_age', String(driverAge), { type: 'sharedFact', fact: 'minimumDriverAge' });

  const related = siblings(current, record), companyEvidence = [];
  for (const jsonPath of companyPaths) {
    const values = related.map(item => ({ ...item, value: store.get(item.snapshot, schema.pointer(jsonPath)) })).filter(item => item.value !== undefined && item.value !== null);
    companyEvidence.push({ jsonPath, values });
    const unique = [...new Set(values.map(item => store.hash(item.value)))];
    if (values.length && unique.length === 1 && store.get({ data }, schema.pointer(jsonPath)) === null)
      put(jsonPath, store.clone(values[0].value), { type: 'brandSiblings', postIds: values.map(item => Number(item.postId)), note: `Same value on every published ${record.identity.country.toUpperCase()} ${label} profile. Confirm it applies to this vehicle.` });
  }

  const draft = { schemaVersion: schema.SCHEMA, draftType: schema.DRAFT_TYPE, vehicleId, identity: store.clone(record.identity), basedOnCatalogRevision: record.catalogRevision,
    createdAt: new Date().toISOString(), target: { siteId: 'motorhomehireProfile', postType: 'vehicle', postStatus: 'draft', language: 'en' }, data, sources };
  return { draft, evidence: evidenceText(workspace, entry, draft, { brand, berths, related, companyEvidence }) };
}

function evidenceText(workspace, entry, draft, { brand, berths, related, companyEvidence }) {
  const { record } = entry, i = record.identity, lines = [];
  lines.push(`# Evidence for ${i.brand} ${i.model} (${i.country.toUpperCase()})`, '',
    'Source material for filling the frontend draft. Use it with the official supplier page; record every value you add under `sources`.', '',
    `- Catalog: \`${path.relative(workspace, entry.file).replace(/\\/g, '/')}\` (${record.vehicleId}, revision ${record.catalogRevision})`,
    `- Identity: ${JSON.stringify(i)}`, `- Berths from identity: ${berths ?? 'unknown'}`,
    `- Brand term: ${brand.term ?? `unresolved${brand.candidates?.length ? ` (used: ${brand.candidates.join(', ')})` : ''}; choose one of ${schema.brandTerms.join(', ')}`}`,
    `- Official URL: \`python ../Brands/brand-registry.py lookup --brand "${i.brand}" --country ${i.country} --model "${i.model}"\``, '');
  lines.push('## Shared facts', '');
  for (const [fact, state] of Object.entries(record.sharedFacts)) lines.push(`- ${fact}: ${shown(state.value)} (${state.status || 'no status'})`);
  lines.push('', `## Same-brand ${i.country.toUpperCase()} Motorhome Hire profiles`, '');
  if (!related.length) lines.push('None published. Supplier-wide fields (licence, inclusions, locations) need the official source.');
  else {
    for (const item of related) lines.push(`- ${item.postId}: ${item.name} (${item.vehicleId})`);
    lines.push('', 'Supplier-wide values on those profiles:', '');
    for (const { jsonPath, values } of companyEvidence) {
      const groups = new Map();
      for (const item of values) { const key = JSON.stringify(item.value); groups.set(key, [...(groups.get(key) || []), item.postId]); }
      lines.push(`- ${jsonPath}: ${groups.size ? [...groups].map(([value, ids]) => `${value} [${ids.join(', ')}]`).join(' / ') : '(none)'}`);
    }
  }
  for (const [siteId, site] of Object.entries(record.sites)) for (const [externalId, external] of Object.entries(site.records)) {
    const s = external.observed.snapshot;
    lines.push('', `## ${siteId} ${externalId}`, '', `Captured ${external.observed.capturedAt || 'unknown'}; ${external.mapping.publicUrl || external.mapping.editUrl}`, '');
    if (siteId === 'motorhomehireAdmin') {
      for (const [key, value] of Object.entries(s.fields)) if (!['_token','vehicle_id','count_image_old','is_main_image_id'].includes(key)) lines.push(`- ${key}: ${typeof value === 'object' ? value.text : shown(value)}`);
      if (s.insurance_table?.rows?.length) lines.push('- insurance_table:', ...s.insurance_table.rows.map(row => `  - ${row.map(cell => Array.isArray(cell) ? `${cell[0] ? 'yes' : 'no'} ${cell[1]}` : cell).join(' | ')}`));
    } else {
      lines.push(`Title: ${s.data.vehicle?.name}`);
      for (const key of ['description','short_description']) if (s.data.descriptionAndImages?.[key]) lines.push('', `### ${key}`, '', htmlText(s.data.descriptionAndImages[key]));
      for (const tab of Object.values(s.data.productTabs?.tabs || {})) if (tab.kind === 'custom' && tab.content) lines.push('', `### ${tab.title}`, '', htmlText(tab.content));
    }
  }
  const duplicates = possibleDuplicates(workspace, draft);
  lines.push('', '## Possible existing Motorhome Hire profiles', '', ...(duplicates.length ? duplicates.map(d => `- ${d.postId}: ${d.name} (${d.where}, ${d.match})`) : ['None found in the catalog or saved captures.']));
  lines.push('', '## Field options', '', ...Object.entries(schema.selectOptions).map(([key, options]) => `- ${key}: ${options.join(' | ')}`),
    `- data.taxonomies.rental_locations (${schema.countryRoot[i.country]}): ${schema.locationTerms.filter(([name]) => { let r = name; while (schema.locationTerms.find(t => t[0] === r)[1]) r = schema.locationTerms.find(t => t[0] === r)[1]; return r === schema.countryRoot[i.country]; }).map(([name]) => name).join(', ')}`);
  return lines.join('\n') + '\n';
}

function possibleDuplicates(workspace, draft) {
  const name = schema.normalize(draft.data.vehicle.name || ''), model = schema.normalize(draft.identity.model), brand = draft.data.taxonomies.brand, found = new Map();
  const consider = (snapshot, where) => {
    const title = schema.normalize(snapshot.data?.vehicle?.name || ''), postId = snapshot.capture?.postId;
    if (!postId || found.has(postId)) return;
    if (name && title === name) found.set(postId, { postId, name: snapshot.data.vehicle.name, where, match: 'same title', exact: true });
    else if (model && title.includes(model) && (!brand || snapshot.data.taxonomies?.brand === brand) && (snapshot.capture.publicUrl || '').includes(draft.identity.country === 'nz' ? 'new-zealand' : draft.identity.country === 'au' ? 'australia' : '/'))
      found.set(postId, { postId, name: snapshot.data.vehicle.name, where, match: 'same brand and model name', exact: false });
  };
  for (const { record } of store.scanCurrent(workspace).values()) for (const { snapshot } of frontendSnapshots(record)) consider(snapshot, `catalog ${record.vehicleId}`);
  for (const file of store.jsonFiles(captureFolder('motorhomehireProfile', workspace))) {
    let snapshot; try { snapshot = store.readJson(file); } catch { continue; }
    consider(snapshot, 'saved capture');
  }
  return [...found.values()];
}

function draftFile(workspace, record) { return store.inside(workspace, `${DRAFTS}/${store.filename(record)}`); }
function companions(file) { const base = file.replace(/\.json$/, ''); return { evidence: `${base}.evidence.md`, report: `${base}.report.txt` }; }

function initDraft(workspace, vehicleId) {
  if (!store.ID.test(vehicleId || '')) throw new Error('Use a permanent catalog ID such as veh_000020.');
  const { draft, evidence } = buildDraft(workspace, vehicleId);
  const file = draftFile(workspace, draft), { evidence: evidenceFile } = companions(file);
  if (fs.existsSync(file)) throw new Error(`A draft already exists: ${path.relative(workspace, file)}. Edit it, or move it away deliberately before starting over.`);
  store.atomicWrite(file, draft);
  store.atomicWrite(evidenceFile, evidence);
  return { file, evidenceFile, draft };
}

function checkDraft(workspace, relative) {
  const file = store.inside(workspace, relative), draft = store.readJson(file), errors = [], warnings = [];
  const top = ['schemaVersion','draftType','vehicleId','identity','basedOnCatalogRevision','createdAt','target','data','sources'];
  const unknown = Object.keys(draft).filter(key => !top.includes(key)), absent = top.filter(key => !Object.hasOwn(draft, key));
  if (unknown.length || absent.length) errors.push(`Draft properties must be exactly: ${top.join(', ')}.`);
  if (draft.schemaVersion !== schema.SCHEMA || draft.draftType !== schema.DRAFT_TYPE) errors.push(`Expected schemaVersion ${schema.SCHEMA} and draftType ${schema.DRAFT_TYPE}.`);
  if (!store.equal(draft.target, { siteId: 'motorhomehireProfile', postType: 'vehicle', postStatus: 'draft', language: 'en' })) errors.push('target must stay the generated motorhomehireProfile / vehicle / draft / en.');
  const catalog = store.scanCurrent(workspace), current = catalog.get(draft.vehicleId);
  if (!current) errors.push(`Catalog vehicle ${draft.vehicleId} no longer exists.`);
  else {
    if (!store.equal(current.record.identity, draft.identity)) errors.push('Catalog identity differs from the draft identity.');
    if (Object.keys(current.record.sites.motorhomehireProfile?.records || {}).length) errors.push('This vehicle is now linked to a Motorhome Hire profile. Use the update workflow.');
    if (current.record.catalogRevision !== draft.basedOnCatalogRevision) warnings.push(`Catalog revision changed (${draft.basedOnCatalogRevision} → ${current.record.catalogRevision}); recheck the evidence.`);
  }
  const shape = schema.shapeErrors(draft.data);
  errors.push(...shape);
  let missing = [];
  if (!shape.length) {
    errors.push(...schema.valueErrors(draft.data, draft.identity?.country), ...schema.sourceErrors(draft));
    missing = schema.missingRequired(draft.data);
    warnings.push(...schema.consistencyWarnings(draft.data, berthsFor(draft.identity || {})));
    for (const duplicate of possibleDuplicates(workspace, draft)) (duplicate.exact ? errors : warnings).push(`Possible existing profile ${duplicate.postId} "${duplicate.name}" (${duplicate.where}, ${duplicate.match}).`);
  }
  // Unknown non-blocking fields are left blank on the website rather than guessed.
  // A brand with no Motorhome Hire term yet (and none on its linked profiles) may stay blank.
  const noBrandTerm = missing.includes('data.taxonomies.brand') && draft.identity && brandTerm(catalog, draft.identity).term === null && !brandTerm(catalog, draft.identity).candidates?.length;
  if (noBrandTerm) warnings.push(`Motorhome Hire has no brand term for ${draft.identity.brand} yet, so the brand is left blank. Set it in WordPress once the term exists.`);
  const blocks = m => schema.blocking.includes(m) && !(noBrandTerm && m === 'data.taxonomies.brand');
  const blocked = missing.filter(blocks), blanks = missing.filter(m => !blocks(m));
  const status = errors.length ? 'invalid' : blocked.length ? 'incomplete' : 'ready';
  const report = reportText(workspace, file, draft, { status, errors, warnings, blocked, blanks, valid: !shape.length });
  store.atomicWrite(companions(file).report, report);
  return { file, status, errors, warnings, missing, blocked, blanks, report };
}

function reportText(workspace, file, draft, { status, errors, warnings, blocked, blanks, valid }) {
  const i = draft.identity || {}, lines = ['Motorhome Hire frontend draft check', `Draft: ${path.relative(workspace, file).replace(/\\/g, '/')}`,
    `Vehicle: ${draft.vehicleId} (${i.country} / ${i.brand} / ${i.model}${i.variant ? ` / ${i.variant}` : ''})`,
    `Status: ${status === 'ready' ? `ready${blanks.length ? ` (${blanks.length} unknown field(s) will be left blank)` : ' (all fields filled)'}` : status === 'incomplete' ? `incomplete (${blocked.length} field(s) the website cannot leave blank)` : `invalid (${errors.length} error(s))`}`,
    'Target: new WordPress vehicle saved as Draft (not published).',
    'Images: not part of this draft. Add the featured image and gallery manually after creation.', ''];
  if (errors.length) lines.push('Errors:', ...errors.map(e => `  - ${e}`), '');
  if (blocked.length) lines.push('Must be filled before creation (the website cannot leave these blank):', ...blocked.map(m => `  - ${m}`), '');
  if (blanks.length) lines.push('Unknown; will be left blank on the website (no source states them):', ...blanks.map(m => `  - ${m}`), '');
  if (warnings.length) lines.push('Warnings:', ...warnings.map(w => `  - ${w}`), '');
  if (valid) {
    lines.push('Fields:');
    for (const jsonPath of schema.allPaths) {
      const value = store.get(draft, schema.pointer(jsonPath)), sources = (draft.sources?.[jsonPath] || []).map(s => s.type).join(', ');
      const text = shown(value);
      lines.push(`  ${jsonPath}: ${text.length > 140 ? text.slice(0, 137) + '...' : text}${sources ? `  [${sources}]` : ''}`);
    }
  }
  return lines.join('\n') + '\n';
}

function main(args = process.argv.slice(2), workspace = root) {
  if (args.length === 1 && args[0] === '--help') { console.log('Draft a new Motorhome Hire frontend vehicle profile (offline):\n  --init VEHICLE_ID   create jobs/frontend-drafts/<vehicle>.json and .evidence.md\n  --check DRAFT.json  validate the draft and write .report.txt'); return; }
  if (args.length !== 2 || !['--init','--check'].includes(args[0])) throw new Error('Use --help for draft commands.');
  if (args[0] === '--init') {
    const r = initDraft(workspace, args[1]);
    console.log(`Draft: ${path.relative(workspace, r.file)}\nEvidence: ${path.relative(workspace, r.evidenceFile)}`);
    return r;
  }
  const r = checkDraft(workspace, args[1]);
  console.log(`${r.status}: ${path.relative(workspace, companions(r.file).report)} (${r.errors.length} error(s), ${r.blocked.length} blocking, ${r.blanks.length} left blank, ${r.warnings.length} warning(s))`);
  if (r.status === 'invalid') process.exitCode = 1;
  return r;
}
if (require.main === module) { try { main(); } catch (error) { console.error(error.message); process.exitCode = 1; } }
module.exports = { DRAFTS, companyPaths, berthsFor, brandTerm, siblings, htmlText, buildDraft, possibleDuplicates, initDraft, checkDraft, main };
