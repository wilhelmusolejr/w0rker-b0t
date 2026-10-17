// Multi-source comparison bundles (schema 3.0) for new Motorhome Hire frontend
// profiles: create a skeleton from a draft, validate it, and apply a reviewed
// bundle's choices into the draft. Offline only. See "Source comparison/AGENTS.md".
const fs = require('node:fs');
const path = require('node:path');
const store = require('./catalog-store.cjs');
const SC = require('../Source comparison/source-compare.js');
const schema = require('./motorhomehire-frontend-schema.cjs');
const drafts = require('./motorhomehire-frontend-draft.cjs');
const { root } = require('./project.cjs');

const MARKETS = { au: 'Australia', nz: 'New Zealand', us: 'USA' };
const bundleFile = draftFile => draftFile.replace(/\.json$/, '.sources.json');
// Draft keeps the site's icon text; bundles use Booleans like the reviewer script.
const ICONS = { 'featuresIcons.toilets': ['Toilet', 'notoilet'], 'featuresIcons.showers': ['Shower', 'noshower'] };
const iconText = (bundlePath, value) => value ? ICONS[bundlePath][0] : `<span class="${ICONS[bundlePath][1]}">${ICONS[bundlePath][0]}</span>`;
// Textareas store "<br>" followed by a newline, matching saved editor values.
const textareaPaths = new Set(SC.FIELDS.filter(f => f.type === 'textarea').map(f => f.path));
function toDraftValue(bundlePath, value) {
  if (value === null) return null;
  if (ICONS[bundlePath]) return iconText(bundlePath, value);
  if (typeof value === 'string') {
    const text = SC.normalizeText(value);
    return textareaPaths.has(bundlePath) ? text.replace(/<br>/g, '<br>\n') : text;
  }
  return Array.isArray(value) ? [...new Set(value.map(v => SC.normalizeText(v)))] : value;
}

function initBundle(workspace, draftRelative) {
  const draftPath = store.inside(workspace, draftRelative), draft = store.readJson(draftPath), out = bundleFile(draftPath);
  if (draft.draftType !== schema.DRAFT_TYPE) throw new Error('Expected a frontend draft created by motorhomehire-frontend-draft.cjs --init.');
  if (fs.existsSync(out)) throw new Error(`A sources bundle already exists: ${path.relative(workspace, out)}.`);
  const bundle = { schemaVersion: '3.0', bundleType: 'vehicle_multi_source_comparison',
    targetVehicle: { workingName: draft.data.vehicle.name, market: MARKETS[draft.identity.country] || null, vehicleId: draft.vehicleId, primarySourceId: null, scrapedAt: new Date().toISOString().slice(0, 10) },
    comparisonPolicy: { nullIsNoClaim: true, autoSelectWhenOneUniqueNonNullValue: true, requireManualSelectionWhenNonNullValuesConflict: true, compareArraysAsUnorderedSets: true, normalizeWhitespaceBeforeComparison: true },
    sources: [], aiProposals: {}, review: { status: 'not_started', selections: {} },
    sourceTemplate: { sourceId: 'replace_me', sourceType: 'official', sourceName: 'Replace me', url: 'https://replace.me/exact-vehicle-page', scrapedAt: bundleDate(), status: 'scraped', notes: [], data: SC.blankData(), evidence: {} } };
  store.atomicWrite(out, bundle);
  return out;
}
function bundleDate() { return new Date().toISOString().slice(0, 10); }

function checkBundle(workspace, relative) {
  const bundle = store.readJson(store.inside(workspace, relative)), errors = [];
  if ('sourceTemplate' in bundle) errors.push('Remove sourceTemplate after copying it into sources.');
  errors.push(...SC.validateBundle(bundle));
  const entries = errors.length ? [] : SC.compareBundle(bundle);
  const warnings = bundle.sources?.flatMap(s => SC.sourceWarnings(s).map(w => `${s.sourceId}: ${w}`)) || [];
  for (const e of entries) {
    if (e.proposal && e.proposalProblems.length) errors.push(`aiProposals.${e.field.path}: ${e.proposalProblems.join(' ')}`);
    else if (e.state === 'conflict' && !e.proposal) warnings.push(`${e.field.path}: conflict without an AI proposal.`);
    for (const c of e.candidates) if (SC.isClaim(c.value)) for (const i of SC.formatIssues(e.field.path, c.value)) if (i.level === 'warning') warnings.push(`${c.sourceId} ${e.field.path}: ${i.message}`);
  }
  const count = state => entries.filter(e => e.state === state).length;
  return { bundle, errors, warnings, entries, counts: { conflict: count('conflict'), agreed: count('agreed'), single: count('single'), none: count('none') } };
}

function draftSource(bundle, sourceId) {
  const s = bundle.sources.find(source => source.sourceId === sourceId);
  return s.sourceType === 'official' ? { type: 'official', url: s.url, note: s.sourceName } : { type: s.sourceType, url: s.url, note: s.sourceName };
}
function applyReview(workspace, reviewedRelative, draftRelative) {
  const checked = checkBundle(workspace, reviewedRelative);
  if (checked.errors.length) throw new Error(`The bundle has errors; run --check:\n- ${checked.errors.join('\n- ')}`);
  const { bundle, entries } = checked, draftPath = store.inside(workspace, draftRelative), draft = store.readJson(draftPath);
  if (bundle.targetVehicle?.vehicleId && bundle.targetVehicle.vehicleId !== draft.vehicleId) throw new Error(`The bundle is for ${bundle.targetVehicle.vehicleId}, not ${draft.vehicleId}.`);
  const problems = [], applied = [], kept = [];
  for (const entry of entries) {
    const selection = bundle.review?.selections?.[entry.field.path];
    const problem = SC.checkSelection(entry, selection);
    if (problem) { problems.push(`${entry.field.path}: ${problem}`); continue; }
    const choice = selection ? { mode: selection.selectionMode, value: selection.value, sourceIds: selection.selectionMode === 'ai' ? entry.proposal.sourceIds : entry.groups.find(g => SC.same(g.value, selection.value))?.sourceIds || [] } : SC.defaultChoice(entry);
    const draftPathKey = `data.${entry.field.path}`;
    if (choice.value === null) { kept.push(draftPathKey); continue; }
    store.set(draft, schema.pointer(draftPathKey), toDraftValue(entry.field.path, choice.value));
    const sources = choice.sourceIds.map(id => draftSource(bundle, id));
    if (choice.mode === 'ai' && entry.proposal.basis === 'derived') sources.unshift({ type: 'derived', note: `AI proposal accepted in review: ${entry.proposal.note}` });
    draft.sources[draftPathKey] = sources;
    applied.push(draftPathKey);
  }
  if (problems.length) throw new Error(`Review is incomplete or does not match the bundle:\n- ${problems.join('\n- ')}`);
  store.atomicWrite(draftPath, draft);
  return { applied, kept, check: drafts.checkDraft(workspace, draftRelative) };
}

function main(args = process.argv.slice(2), workspace = root) {
  if (args.length === 1 && args[0] === '--help') { console.log('Multi-source bundles for new Motorhome Hire frontend profiles (offline):\n  --init DRAFT.json              create <draft>.sources.json to fill per "Source comparison/AGENTS.md"\n  --check BUNDLE.json            validate sources, formats and AI proposals\n  --apply REVIEWED.json DRAFT.json  write reviewed choices into the draft, then check it\nReview bundles in "Source comparison/index.html".'); return; }
  if (args[0] === '--init' && args.length === 2) { const out = initBundle(workspace, args[1]); console.log(`Bundle: ${path.relative(workspace, out)}`); return out; }
  if (args[0] === '--check' && args.length === 2) {
    const r = checkBundle(workspace, args[1]);
    for (const e of r.errors) console.log(`ERROR ${e}`);
    for (const w of r.warnings) console.log(`warning ${w}`);
    console.log(`${r.errors.length ? 'invalid' : 'valid'}: ${r.counts.conflict} conflict(s), ${r.counts.agreed} agreed, ${r.counts.single} single-source, ${r.counts.none} without data; ${r.errors.length} error(s), ${r.warnings.length} warning(s).`);
    if (r.errors.length) process.exitCode = 1;
    return r;
  }
  if (args[0] === '--apply' && args.length === 3) {
    const r = applyReview(workspace, args[1], args[2]);
    console.log(`Applied ${r.applied.length} field(s); ${r.kept.length} had no source value and kept the draft value.\nDraft ${r.check.status}: ${path.relative(workspace, r.check.file.replace(/\.json$/, '.report.txt'))}`);
    return r;
  }
  throw new Error('Use --help for bundle commands.');
}
if (require.main === module) { try { main(); } catch (error) { console.error(error.message); process.exitCode = 1; } }
module.exports = { bundleFile, toDraftValue, initBundle, checkBundle, applyReview, main };
