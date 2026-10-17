// Add New Vehicle page controls (post-new.php?post_type=vehicle). Execution
// and approval belong to create-motorhomehire-frontend.cjs.
const store = require('./catalog-store.cjs');
const mhEditor = require('./motorhomehire-profile-editor.cjs');
const { specialFeatureLabels } = require('./motorhomehire-profile-schema.cjs');
const schema = require('./motorhomehire-frontend-schema.cjs');

const SITE = 'https://motorhomehire.com';
const addUrl = `${SITE}/wp-admin/post-new.php?post_type=vehicle&lang=en`;
const editUrl = postId => `${SITE}/wp-admin/post.php?post=${postId}&action=edit&lang=en`;

// Blank optional values are left untouched on the empty Add form.
function operations(draft) {
  return schema.formFields.map(field => ({ ...field, value: store.get(draft, schema.pointer(field.jsonPath)) }))
    .filter(op => op.value !== null && op.value !== '');
}
function terms(draft) {
  const t = draft.data.taxonomies;
  return { brand: t.brand ? [t.brand] : [], mh_location: t.rental_locations, 'special-features': schema.specialFeatures.filter(key => t.special_features[key]).map(key => specialFeatureLabels[key]) };
}
async function openAdd(page, timeout) {
  await page.goto(addUrl, { waitUntil: 'load', timeout });
  const url = new URL(page.url());
  if (url.origin !== SITE || url.pathname !== '/wp-admin/post-new.php' || url.searchParams.get('post_type') !== 'vehicle') throw new Error('The Add New Vehicle page did not open. Check the WordPress login in Hidemium.');
  await page.locator('#title').waitFor({ state: 'visible', timeout });
  if (await page.locator('[name="post_type"]').inputValue() !== 'vehicle') throw new Error('Unexpected post type on the Add page. Refusing to fill.');
  const postId = Number(await page.locator('[name="post_ID"]').inputValue());
  if (!Number.isSafeInteger(postId) || postId <= 0) throw new Error('The Add page has no new post ID.');
  const status = await page.locator('[name="original_post_status"]').inputValue().catch(() => null);
  if (status !== 'auto-draft') throw new Error(`Expected a new auto-draft post, found status ${status}.`);
  const language = await page.locator('#wp-admin-bar-WPML_ALS .ab-item span[title]').first().getAttribute('title', { timeout }).catch(() => null);
  if (!/content in: English/i.test(language || '')) throw new Error(`The editor is not showing English content (${language || 'no language switcher found'}).`);
  if (await page.locator('#title').inputValue()) throw new Error('The Add page title is not empty.');
  // WordPress autosave would turn the auto-draft into a title-only draft while
  // the form is being filled, even if the run later stops before Save.
  await page.route('**/wp-admin/admin-ajax.php', route => /wp_autosave/.test(decodeURIComponent(route.request().postData() || '')) ? route.abort() : route.fallback());
  await page.evaluate(() => window.wp?.autosave?.server?.suspend?.());
  return postId;
}
async function termInputs(page, taxonomy) {
  const inputs = page.locator(`#${taxonomy}checklist input[type="checkbox"][name="tax_input[${taxonomy}][]"]`);
  const labels = await inputs.evaluateAll(elements => elements.map(el => el.closest('label')?.textContent.replace(/\s+/g, ' ').trim()));
  if (!labels.length) throw new Error(`Taxonomy checklist missing: ${taxonomy}.`);
  return { inputs, labels };
}
async function setTerms(page, taxonomy, wanted) {
  const { inputs, labels } = await termInputs(page, taxonomy);
  for (const label of wanted) if (labels.filter(l => l === label).length !== 1) throw new Error(`Expected one ${taxonomy} term "${label}" on the Add page.`);
  for (let i = 0; i < labels.length; i++) {
    const want = wanted.includes(labels[i]);
    if (await inputs.nth(i).isChecked() !== want) await inputs.nth(i).setChecked(want);
  }
}
async function readTerms(page, taxonomy) {
  const { inputs, labels } = await termInputs(page, taxonomy);
  const checked = await inputs.evaluateAll(elements => elements.map(el => el.checked));
  return labels.filter((label, i) => checked[i]);
}
const sameSet = (a, b) => store.equal([...a].sort(), [...b].sort());

async function fill(page, draft) {
  for (const op of operations(draft)) await mhEditor.writeOperation(page, op);
  for (const [taxonomy, wanted] of Object.entries(terms(draft))) await setTerms(page, taxonomy, wanted);
  return verifyForm(page, draft);
}
// Reads back every filled control before anything is saved.
async function verifyForm(page, draft) {
  const form = {}, mismatches = [];
  for (const op of operations(draft)) {
    const live = await mhEditor.readOperation(page, op), want = await mhEditor.desiredValue(page, op);
    form[op.jsonPath] = live;
    if (!store.equal(typeof live === 'string' ? live.replace(/\r\n?/g, '\n') : live, want)) mismatches.push(op.jsonPath);
  }
  const filled = new Set(operations(draft).map(op => op.jsonPath));
  for (const field of schema.formFields.filter(field => field.type !== 'select' && !filled.has(field.jsonPath))) {
    const live = await mhEditor.readOperation(page, field), clean = typeof live === 'string' ? live.replace(/\r\n?/g, '\n') : live;
    if (clean !== '' && clean !== null && clean !== schema.addDefaults[field.jsonPath]) mismatches.push(`${field.jsonPath} (expected blank)`);
  }
  for (const [taxonomy, wanted] of Object.entries(terms(draft))) {
    form[taxonomy] = await readTerms(page, taxonomy);
    if (!sameSet(form[taxonomy], wanted)) mismatches.push(`taxonomy ${taxonomy}`);
  }
  if (mismatches.length) throw new Error(`The Add page did not keep: ${mismatches.join(', ')}. Nothing was saved.`);
  return form;
}
// Compares the reloaded saved editor capture with the approved draft.
function verifySaved(draft, snapshot) {
  const mismatches = [];
  const clean = value => typeof value === 'string' ? value.replace(/\r\n?/g, '\n') : value;
  for (const field of schema.formFields) {
    const want = store.get(draft, schema.pointer(field.jsonPath)), live = store.get(snapshot, schema.pointer(field.jsonPath));
    const expected = want === '' || want === null ? schema.addDefaults[field.jsonPath] ?? null : want;
    if (!store.equal(clean(live) ?? null, clean(expected) ?? null)) mismatches.push(field.jsonPath);
  }
  const t = draft.data.taxonomies, s = snapshot.data.taxonomies;
  if (s.brand !== t.brand) mismatches.push('data.taxonomies.brand');
  if (!sameSet(s.rental_locations, t.rental_locations)) mismatches.push('data.taxonomies.rental_locations');
  // An unknown special feature is simply not ticked.
  for (const key of schema.specialFeatures) if (s.special_features[key] !== (t.special_features[key] ?? false)) mismatches.push(`data.taxonomies.special_features.${key}`);
  if (snapshot.capture.postStatus !== 'draft') mismatches.push(`capture.postStatus (${snapshot.capture.postStatus})`);
  if (mismatches.length) throw new Error(`Saved vehicle differs from the approved draft: ${mismatches.join(', ')}.`);
}
// Live list search for an existing vehicle with the same title, in any language or status.
async function findTitle(page, name, timeout) {
  const url = `${SITE}/wp-admin/edit.php?post_type=vehicle&post_status=all&lang=all&s=${encodeURIComponent(name)}`;
  await page.goto(url, { waitUntil: 'domcontentloaded', timeout });
  if (new URL(page.url()).pathname !== '/wp-admin/edit.php') throw new Error('The vehicle list did not open. Check the WordPress login in Hidemium.');
  await page.locator('#the-list').waitFor({ state: 'attached', timeout });
  const rows = await page.locator('#the-list tr[id^="post-"]').evaluateAll(rows => rows.map(row => ({ postId: Number(row.id.slice(5)), title: row.querySelector('a.row-title')?.textContent.trim() || '' })));
  return rows.filter(row => schema.normalize(row.title) === schema.normalize(name));
}

module.exports = { SITE, addUrl, editUrl, operations, terms, openAdd, setTerms, readTerms, fill, verifyForm, verifySaved, findTitle };
