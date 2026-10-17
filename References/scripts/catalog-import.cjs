const fs = require('node:fs');
const path = require('node:path');
const { randomUUID } = require('node:crypto');
const store = require('./catalog-store.cjs');
const fields = require('./catalog-fields.cjs');
const { loadSettings } = require('./catalog-plan.cjs');

function importVehicles(root, manifestFile) {
  const manifest = store.readJson(store.inside(root, manifestFile));
  if (manifest.schemaVersion !== '1.0' || manifest.exampleOnly || !Array.isArray(manifest.vehicles) || !manifest.vehicles.length) throw new Error('Expected an import schema 1.0 with reviewed vehicles/sources. Copy and complete the example manifest first.');
  const { policies } = loadSettings(root);
  return store.withLock(root, 'import', () => {
    const current = store.scanCurrent(root), seenIds = new Set(current.keys()), seenLinks = new Set();
    for (const journal of store.jsonFiles(store.inside(root,'catalog/import-results'))) {
      for (const imported of store.readJson(journal).imported || []) seenIds.add(imported.vehicleId);
    }
    for (const entry of current.values()) for (const [siteId, site] of Object.entries(entry.record.sites)) for (const id of Object.keys(site.records)) seenLinks.add(`${siteId}:${id}`);
    const prepared = manifest.vehicles.map(input => {
      if (!store.ID.test(input.vehicleId || '') || seenIds.has(input.vehicleId)) throw new Error('Import IDs must be new, permanent and unique. Existing records use capture incorporation instead of replacement.');
      seenIds.add(input.vehicleId);
      if (!Array.isArray(input.targets) || !input.targets.length) throw new Error('Each imported vehicle needs explicitly linked sources.');
      const record = { schemaVersion: '1.0', recordType: 'vehicleCatalogRecord', vehicleId: input.vehicleId, catalogRevision: 1,
        identity: store.clone(input.identity), sharedFacts: Object.fromEntries(Object.keys(policies.facts).map(fact => [fact, { value: null, status: 'unresolved', evidence: [] }])),
        sites: Object.fromEntries([...Object.keys(fields.SITES), 'campervanfinderSearchAdmin'].map(siteId => [siteId, { availability: 'notLinked', records: {} }])),
        reconciliation: { status: 'reviewRequired', issues: [] },
      };
      for (const target of input.targets) {
        const { siteId, externalId, sourceFile, role, confirmed } = target, site = fields.SITES[siteId];
        if (!site || !/^[1-9]\d*$/.test(String(externalId)) || !Number.isSafeInteger(Number(externalId)) || typeof sourceFile !== 'string' || typeof role !== 'string' || !role.trim() || role === 'unassigned' || typeof confirmed !== 'boolean') throw new Error('Each source needs a supported siteId, externalId, sourceFile, role and confirmed boolean.');
        const key = `${siteId}:${externalId}`;
        if (seenLinks.has(key)) throw new Error(`External source is already linked: ${key}`);
        seenLinks.add(key);
        const snapshot = fields.validateSnapshot(siteId, store.readJson(store.inside(root, sourceFile)), String(externalId));
        if (fields.isCampervanFinder(siteId) && record.identity.country !== site.country) throw new Error(`A ${site.country.toUpperCase()} Campervan Finder profile needs a ${site.country.toUpperCase()} catalog identity.`);
        const observed = store.observation(snapshot);
        observed.captureStatus = 'imported'; observed.importSource = sourceFile;
        record.sites[siteId].availability = 'linked';
        record.sites[siteId].records[String(externalId)] = {
          mapping: { status: confirmed ? 'confirmed' : 'candidate', role, entityType: site.entityType, siteOrigin: site.origin,
            editUrl: snapshot.capture?.editUrl || snapshot.source.url, publicUrl: snapshot.capture?.publicUrl || snapshot.fields?.detail_link || null,
            ...(target.factBindings ? { factBindings: store.clone(target.factBindings) } : {}),
          }, observed, lastReadAttempt: null,
        };
      }
      return store.validateRecord(fields.reconcile(record, policies));
    });
    // Validate every source before creating any current file. Record successful imports for recovery.
    const journal = store.inside(root,`catalog/import-results/IMPORT-${new Date().toISOString().replace(/\D/g,'')}-${randomUUID().slice(0,8)}.json`);
    const result = { manifestFile, manifestHash:store.hash(manifest), imported: [], pending: prepared.map(record => record.vehicleId) };
    store.atomicWrite(journal,result);
    try { for (const record of prepared) store.withLock(root, record.vehicleId, () => {
      const destination = store.inside(root, `catalog/current/${record.identity.country}/${store.filename(record)}`);
      if (fs.existsSync(destination)) throw new Error('Current import destination appeared concurrently.');
      store.persist(root, record);
      result.imported.push({ vehicleId: record.vehicleId, file: path.relative(root, destination), mappingStatus: Object.values(record.sites).flatMap(site => Object.values(site.records)).every(external => external.mapping.status === 'confirmed') ? 'confirmed' : 'candidate' });
      result.pending = result.pending.filter(id => id !== record.vehicleId);
      store.atomicWrite(journal,result);
    }); } catch(error) { result.error=error.message; store.atomicWrite(journal,result); throw new Error(`${error.message} Import recovery: ${path.relative(root,journal)}`); }
    return result;
  });
}

module.exports = { importVehicles };
