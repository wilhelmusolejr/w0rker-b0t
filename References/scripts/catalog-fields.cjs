const { get, set, clone, equal, hash, sanitize } = require('./catalog-store.cjs');
const mh = require('./motorhomehire-profile-schema.cjs');
const cf = require('./campervanfinder-schema.cjs');
const backend = require('./motorhomehire-search-schema.cjs');

const SITES = {
  motorhomehireProfile: { label: 'Motorhome Hire vehicle profile', origin: 'https://motorhomehire.com', entityType: 'vehicle' },
  motorhomehireAdmin: { label: 'Motorhome Hire search record', origin: backend.SITE, entityType: 'backendVehicle' },
  campervanfinderAuProfile: { label: 'Campervan Finder AU vehicle profile', origin: cf.countrySite('au').origin, entityType: 'product', country: 'au' },
  campervanfinderNzProfile: { label: 'Campervan Finder NZ vehicle profile', origin: cf.countrySite('nz').origin, entityType: 'product', country: 'nz' },
};
const isCampervanFinder = siteId => siteId === 'campervanfinderAuProfile' || siteId === 'campervanfinderNzProfile';
const DEFAULT_POLICIES = {
  schemaVersion: '1.0',
  facts: {
    vehicleAge: { type: 'vehicleAge', authority: 'motorhomehireProfile', autoAccept: false, bindings: {
      motorhomehireProfile: [
        { path: '/data/vehicleSpecifications/vehicle_age', renderer: 'ageText' },
        { path: '/data/featuresIcons/vehicle_ages', renderer: 'ageText' },
      ],
      motorhomehireAdmin: [
        { path: '/fields/age_detail', renderer: 'ageDetail' },
        { path: '/fields/age_vehicle', renderer: 'ageMaxYears' },
      ],
      campervanfinderAuProfile: [{ renderer: 'ageHtmlLabel', label: 'Vehicle Age' }],
      campervanfinderNzProfile: [{ renderer: 'ageHtmlLabel', label: 'Vehicle Age' }],
    } },
    minimumDriverAge: { type: 'integer', authority: 'motorhomehireProfile', autoAccept: false, bindings: {
      motorhomehireProfile: [{ path: '/data/importantInformation/minimum_driver_age', renderer: 'integerText' }],
    } },
  },
};

function backendSnapshot(snapshot) {
  const copy = clone(snapshot);
  copy.fields = { ...copy.fields, _token: copy.fields?._token || '' };
  return backend.validate(copy);
}
function validateSnapshot(siteId, snapshot, externalId) {
  const site = SITES[siteId];
  if (!site) throw new Error(`Unsupported target: ${siteId}.`);
  if (siteId === 'motorhomehireAdmin') {
    backendSnapshot(snapshot);
    if (snapshot.source.vehicle_id !== String(externalId)) throw new Error('Search-record ID does not match its mapping.');
  } else {
    const c = snapshot.capture;
    if (c?.postId !== Number(externalId) || c.postType !== site.entityType || c.siteUrl !== site.origin) throw new Error(`Wrong ${site.label} identity.`);
    const url = new URL(c.editUrl);
    if (url.origin !== site.origin || url.pathname !== '/wp-admin/post.php' || url.searchParams.get('post') !== String(externalId) || url.searchParams.get('action') !== 'edit') throw new Error('Wrong mapped editor URL.');
    if (!['publish', 'private', 'draft', 'pending'].includes(c.postStatus)) throw new Error('Missing or unsupported publication state.');
    if (isCampervanFinder(siteId)) cf.validateSnapshot(snapshot, { country: site.country });
    else {
      if (snapshot.schemaVersion !== '1.1' || snapshot.bundleType !== 'vehicle_profile_snapshot' || c.source !== 'current_wordpress_vehicle_editor') throw new Error('Expected a captured Motorhome Hire schema 1.1 profile.');
      if (snapshot.audit?.sampleOnly || snapshot.audit?.missingControls?.length || snapshot.audit?.readErrors?.length || snapshot.audit?.completeness?.missingControls !== 0) throw new Error('Motorhome Hire capture is incomplete or reference-only.');
      for (const field of mh.fieldMappings.filter(f => ['text','textarea','select','checkbox'].includes(f.type))) {
        if (get(snapshot, '/' + field.jsonPath.replace(/\./g, '/')) === undefined) throw new Error(`Missing captured field: ${field.jsonPath}`);
      }
      const gallery = snapshot.data?.descriptionAndImages?.image_gallery;
      if (!gallery || !Array.isArray(gallery.images) || gallery.image_count !== gallery.images.length || typeof snapshot.data.vehicle.name !== 'string' || !snapshot.data.vehicle.name.trim()) throw new Error('Incomplete name/gallery capture.');
    }
  }
  return snapshot;
}
function mappingFor(siteId, path, baseline, value) {
  if (!path.startsWith('/data/') && !path.startsWith('/fields/')) throw new Error(`Only editable data paths may change: ${path}`);
  if (get(baseline, path) === undefined || value === undefined) throw new Error(`Adding/removing fields is unsupported: ${path}. Use an explicit blank value to clear text.`);
  const jsonPath = path.slice(1).replace(/\//g, '.');
  if (siteId === 'motorhomehireAdmin') {
    if (!backend.updateFields.some(key => path === `/fields/${key}`)) throw new Error(`Unsupported search-record edit: ${path}. Only age_vehicle, age_detail and detail_link are writable.`);
    if (typeof value !== 'string') throw new Error('Search-record editable values remain strings.');
    const desired = backendSnapshot(baseline); set(desired, path, value);
    backend.changes(backendSnapshot(baseline), desired);
    return { field: jsonPath, before: get(baseline, path), after: value };
  }
  if (isCampervanFinder(siteId)) {
    const mapping = cf.resolveMapping(jsonPath, baseline); cf.validateValue(mapping, value); return mapping;
  }
  const mapping = mh.fieldMappings.find(field => field.jsonPath === jsonPath);
  if (!mapping || !['text','textarea','select','checkbox'].includes(mapping.type)) throw new Error(`Unsupported vehicle-profile edit: ${path}. Media and taxonomies remain read-only.`);
  if (mapping.type === 'checkbox' ? typeof value !== 'boolean' : value !== null && value !== false && !['string','number'].includes(typeof value)) throw new Error(`Invalid value for ${path}.`);
  if (typeof value === 'number' && !Number.isFinite(value)) throw new Error(`Invalid numeric text value for ${path}.`);
  if (jsonPath === 'data.vehicle.name' && (typeof value !== 'string' || !value.trim())) throw new Error('Vehicle name must be nonempty.');
  return { ...mapping };
}
function guards(siteId, snapshot) {
  if (siteId === 'motorhomehireAdmin') return { vehicle_id: snapshot.source.vehicle_id, url: snapshot.source.url };
  return { postId: snapshot.capture.postId, postType: snapshot.capture.postType, siteUrl: snapshot.capture.siteUrl,
    postStatus: snapshot.capture.postStatus, editUrl: snapshot.capture.editUrl, language: snapshot.capture.language ?? null, locale: snapshot.capture.locale ?? null, tabs: isCampervanFinder(siteId)
      ? Object.fromEntries(Object.entries(snapshot.data.productTabs.tabs).map(([index, tab]) => [index, { tabId: tab.tabId, kind: tab.kind, position: tab.position }])) : null };
}
function validateGuards(target, snapshot) {
  validateSnapshot(target.siteId, snapshot, target.externalId);
  if (!equal(guards(target.siteId, target.baseline), guards(target.siteId, snapshot))) throw new Error('Record identity, publication state or tab structure changed since review.');
}
function normalized(siteId, path, value) {
  if (typeof value === 'string') {
    const text = value.replace(/\r\n?/g, '\n');
    // The Campervan Finder custom-tab plugin appends this terminal break when
    // saving a table. Compare the table itself exactly; retain the actual saved
    // source in captures and the catalog, including the plugin's addition.
    if (isCampervanFinder(siteId) && /^\/data\/productTabs\/tabs\/\d+\/content$/.test(path) && /^<table\b/i.test(text)) {
      return text.replace(/(<\/table>)\n<br \/>(?:\n)?$/i, '$1');
    }
    return text;
  }
  if (siteId === 'motorhomehireProfile' && typeof value === 'number') return String(value);
  const boolean = siteId === 'motorhomehireProfile' ? mh.fieldMappings.find(field => '/' + field.jsonPath.replace(/\./g, '/') === path)?.type === 'checkbox'
    : path === '/data/productTabs/override_layout' || path.startsWith('/data/taxonomies/product_categories/');
  if (siteId !== 'motorhomehireAdmin' && !boolean && (value === null || value === false)) return '';
  return value;
}
function checkOperations(target, snapshot, { desired = false } = {}) {
  validateGuards(target, snapshot);
  for (const op of target.operations) {
    mappingFor(target.siteId, op.path, snapshot, op.value);
    const live = normalized(target.siteId, op.path, get(snapshot, op.path));
    const want = normalized(target.siteId, op.path, op.value);
    const original = normalized(target.siteId, op.path, op.before);
    if (!equal(live, want) && (desired || !equal(live, original))) throw new Error(`${desired ? 'Saved value mismatch' : 'Live field conflict'}: ${op.path}.`);
  }
}

function age(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || !equal(Object.keys(value).sort(), ['kind','max','min']) || !['years','modelYears'].includes(value.kind) || !Number.isInteger(value.min) || !Number.isInteger(value.max) || value.min > value.max) throw new Error('vehicleAge.value must be {"kind":"years" or "modelYears","min":NUMBER,"max":NUMBER}.');
  if (value.kind === 'years' && (value.min < 0 || value.max > 150) || value.kind === 'modelYears' && (value.min < 1900 || value.max > 2200)) throw new Error('Vehicle age range is invalid.');
  return value;
}
function parseAge(text) {
  const source = String(text ?? '').trim();
  const range = /^(\d{1,4})(?:\s*[-–]\s*(\d{1,4}))?\s*(years?|models?)?$/i.exec(source);
  if (!range) return null;
  const min = Number(range[1]), max = Number(range[2] || range[1]);
  const kind = min >= 1900 && max >= 1900 ? 'modelYears' : 'years';
  if (kind === 'modelYears' && /^years?$/i.test(range[3] || '') || kind === 'years' && /^models?$/i.test(range[3] || '')) return null;
  try { return age({ kind, min, max }); } catch { return null; }
}
const rangeText = value => value.min === value.max ? String(value.min) : `${value.min}-${value.max}`;
const escapeHtml = text => text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
function htmlAgeBinding(snapshot, label) {
  if (typeof label !== 'string' || !label.trim()) throw new Error('HTML age binding needs an exact label.');
  const literal = label.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const matches = [];
  for (const [index, tab] of Object.entries(snapshot.data.productTabs.tabs)) {
    if (tab.kind !== 'custom' || typeof tab.content !== 'string') continue;
    const regex = new RegExp(`(${literal}\\s*:\\s*)([^<>]*)(?=<br\\s*\\/?>|<\\/p>)`, 'gi');
    for (const match of tab.content.matchAll(regex)) matches.push({ path: `/data/productTabs/tabs/${index}/content`, content: tab.content, start: match.index + match[1].length, end: match.index + match[0].length, raw: match[2] });
  }
  if (matches.length > 1) throw new Error(`Ambiguous HTML age label: ${label}. Configure a specific reviewed binding.`);
  return matches[0];
}
function validatePolicies(policies) {
  if (policies?.schemaVersion !== '1.0' || !policies.facts || typeof policies.facts !== 'object') throw new Error('Expected field-policies schema 1.0.');
  for (const [fact, policy] of Object.entries(policies.facts)) {
    if (!['vehicleAge','integer'].includes(policy.type) || !SITES[policy.authority] || typeof policy.autoAccept !== 'boolean' || !policy.bindings) throw new Error(`Invalid policy: ${fact}`);
    for (const [siteId, bindings] of Object.entries(policy.bindings)) {
      if (!SITES[siteId] || !Array.isArray(bindings) || !bindings.length) throw new Error(`Invalid binding target for ${fact}.`);
      for (const binding of bindings) if (!['ageText','ageDetail','ageMaxYears','ageHtmlLabel','integerText'].includes(binding.renderer) || binding.renderer !== 'ageHtmlLabel' && typeof binding.path !== 'string') throw new Error(`Unsupported renderer for ${fact}.`);
    }
  }
  return policies;
}
function renderFact(fact, value, target, policies) {
  const policy = policies.facts[fact];
  if (!policy) throw new Error(`Shared fact has no write policy: ${fact}. Use a supported site-specific edit or configure its mappings.`);
  if (policy.type === 'vehicleAge') age(value);
  else if (!Number.isInteger(value) || value < 0) throw new Error(`${fact} requires a nonnegative integer.`);
  const bindings = target.mapping.factBindings?.[fact] || policy.bindings[target.siteId];
  const operations = [], notes = [];
  if (!bindings?.length) return { operations, notes: [{ fact, reason: 'No supported field mapping on this target.' }] };
  for (const binding of bindings) {
    let path = binding.path, desired;
    if (binding.renderer === 'integerText' && policy.type === 'integer') desired = String(value);
    else if (policy.type === 'vehicleAge') {
      const label = rangeText(value);
      if (binding.renderer === 'ageText') desired = `${label}${value.kind === 'years' ? value.max === 1 ? ' year' : ' years' : ''}`;
      else if (binding.renderer === 'ageDetail') desired = label;
      else if (binding.renderer === 'ageMaxYears') {
        if (value.kind === 'modelYears') { notes.push({ fact, path, reason: 'Model years do not establish a numeric vehicle age; retained current numeric age.' }); continue; }
        desired = String(value.max);
      } else if (binding.renderer === 'ageHtmlLabel' && isCampervanFinder(target.siteId)) {
        const slot = htmlAgeBinding(target.baseline, binding.label);
        if (!slot) { notes.push({ fact, reason: `No unambiguous editable ${binding.label} entry in the captured HTML.` }); continue; }
        path = slot.path;
        const text = value.kind === 'years' ? `${label}${value.max === 1 ? ' year' : ' years'}` : `${label} Models`;
        desired = slot.content.slice(0, slot.start) + escapeHtml(text) + slot.content.slice(slot.end);
      } else throw new Error(`Unsupported binding for ${fact}: ${binding.renderer}.`);
    } else throw new Error(`Renderer/type mismatch: ${fact}.`);
    const before = get(target.baseline, path);
    mappingFor(target.siteId, path, target.baseline, desired);
    operations.push({ path, before, value: desired, sources: [{ type: 'sharedFact', fact }] });
  }
  return { operations, notes };
}
function reconcile(record, policies) {
  record.reconciliation ||= { issues: [] };
  const retained = (record.reconciliation.issues || []).filter(issue => issue.type !== 'factComparison');
  for (const [fact, policy] of Object.entries(policies.facts)) {
    const evidence = [];
    for (const [siteId, site] of Object.entries(record.sites)) for (const [externalId, external] of Object.entries(site.records)) {
      const bindings = external.mapping.factBindings?.[fact] || policy.bindings[siteId] || [];
      for (const binding of bindings) {
        const snapshot = external.observed.snapshot;
        let raw, pointer = binding.path;
        if (binding.renderer === 'ageHtmlLabel') {
          let slot;
          try { slot = htmlAgeBinding(snapshot, binding.label); } catch { continue; }
          if (!slot) continue;
          raw = slot.raw; pointer = slot.path;
        } else if (binding.renderer === 'ageMaxYears') continue;
        else raw = get(snapshot, pointer);
        const parsed = policy.type === 'vehicleAge' ? parseAge(raw) : /^\d+$/.test(String(raw ?? '')) ? Number(raw) : null;
        if (parsed !== null) evidence.push({ site: siteId, externalId, path: pointer, rawValue: raw, parsedValue: parsed, snapshotHash: external.observed.captureHash, capturedAt: external.observed.capturedAt, parserVersion: 'catalogFacts.v1' });
      }
    }
    const authority = evidence.filter(item => item.site === policy.authority);
    const unique = [...new Set(authority.map(item => hash(item.parsedValue)))];
    const existing = record.sharedFacts[fact] || { value: null };
    const value = unique.length === 1 ? authority[0].parsedValue : existing.value;
    record.sharedFacts[fact] = { ...existing, value, status: unique.length === 1 ? policy.autoAccept ? 'accepted' : 'candidate' : 'unresolved', evidence };
    if (unique.length !== 1 || evidence.some(item => !equal(item.parsedValue, value))) retained.push({ type: 'factComparison', fact, reason: unique.length !== 1 ? 'Authoritative fact is absent or ambiguous.' : 'Captured websites disagree.', automaticPropagation: false });
  }
  // Existing evidence for other facts must not retain a hash that labels old values as fresh.
  for (const [fact, state] of Object.entries(record.sharedFacts)) if (!policies.facts[fact]) {
    for (const evidence of state.evidence || []) {
      const current = record.sites[evidence.site]?.records[evidence.externalId]?.observed;
      if (current && evidence.snapshotHash !== current.captureHash) {
        if (equal(get(current.snapshot, evidence.path), evidence.rawValue)) { evidence.snapshotHash = current.captureHash; evidence.capturedAt = current.capturedAt; }
        else state.status = 'needsReview';
      }
    }
  }
  record.reconciliation.issues = retained;
  record.reconciliation.status = retained.length ? 'reviewRequired' : 'captured';
  return record;
}

module.exports = { SITES, isCampervanFinder, DEFAULT_POLICIES, backendSnapshot, validateSnapshot, mappingFor, guards, validateGuards, normalized, checkOperations, age, parseAge, htmlAgeBinding, validatePolicies, renderFact, reconcile };
