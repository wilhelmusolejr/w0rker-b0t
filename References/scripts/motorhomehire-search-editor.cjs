const { isDeepStrictEqual: equal } = require('node:util');
const { SITE, editable, updateFields, validate, fingerprint } = require('./motorhomehire-search-schema.cjs');
const log = s => console.log(`[${new Date().toISOString()}] ${s}`);
const sameField = (key, a, b) => ['top_insurance','terms_conditions'].includes(key) && typeof a === 'string' && typeof b === 'string' ? a.replace(/\r\n?/g, '\n').trimEnd() === b.replace(/\r\n?/g, '\n').trimEnd() : equal(a,b);

// Executed inside the authenticated page; never reuse a token from proposed JSON.
function formOperation({ keys, edits = [] }) {
  if (edits.some(edit => !['fields.detail_link', 'fields.age_detail', 'fields.age_vehicle'].includes(edit.field))) throw new Error('Only detail_link, age_detail, and age_vehicle may be updated.');
  const form = document.querySelector('#submit_ajax_vehicle');
  if (!form) throw new Error('Backend vehicle form is missing.');
  const control = key => {
    const matches = form.querySelectorAll(`[name="${key}"]`);
    if (matches.length !== 1) throw new Error(`Missing/duplicate backend control: ${key}`);
    return matches[0];
  };
  const set = (el, value) => {
    if (el.disabled || el.readOnly) throw new Error(`Control is not editable: ${el.name}`);
    if (el.type === 'checkbox') el.checked = value;
    else el.value = value;
    el.dispatchEvent(new Event('input', { bubbles: true }));
    el.dispatchEvent(new Event('change', { bubbles: true }));
  };
  const table = document.querySelector('#insurance_table');
  if (!table) throw new Error('Insurance table is missing.');
  const tableRows = section => [...table.querySelectorAll(`${section} > tr`)];
  const cells = row => [...row.children].filter(c => ['TD','TH'].includes(c.tagName)).filter(c => c.querySelector('input,textarea'));
  const cellControls = cell => {
    const checkbox = cell.querySelector('input[type="checkbox"]');
    const texts = [...cell.querySelectorAll('input:not([type="checkbox"]):not([type="hidden"]):not([type="button"]), textarea')];
    if (texts.length !== 1 || cell.querySelectorAll('input[type="checkbox"]').length > 1) throw new Error('Unrecognized insurance cell controls. Inspect the live table before editing.');
    return { checkbox, text: texts[0] };
  };
  const readTable = () => Object.fromEntries([['headers','thead'],['rows','tbody']].map(([key,section]) => [key, tableRows(section).map(row => cells(row).map(cell => {
    const c = cellControls(cell); return c.checkbox ? [c.checkbox.checked, c.text.value] : c.text.value;
  }))]));
  // Resolve every target and validate options/shape before mutating any control.
  const targets = edits.map(edit => {
    if (edit.field === 'insurance_table') {
      const current = readTable();
      const shape = t => [t.headers.map(r => r.map(c => Array.isArray(c))), t.rows.map(r => r.map(c => Array.isArray(c)))];
      if (JSON.stringify(shape(current)) !== JSON.stringify(shape(edit.after))) throw new Error('Live insurance table structure changed.');
      return { edit };
    }
    const key = edit.field.slice(7), el = control(key);
    const editor = window.CKEDITOR?.instances?.[key];
    if (el.disabled || el.readOnly) throw new Error(`Control is not editable: ${key}`);
    if (el.tagName === 'SELECT') {
      const option = [...el.options].find(o => o.value === edit.after.value);
      if (!option || option.disabled || option.textContent.trim() !== edit.after.text.trim()) throw new Error(`Dropdown ID/label no longer matches: ${key}`);
    }
    if (el.tagName === 'TEXTAREA' && getComputedStyle(el).display === 'none' && !editor) throw new Error(`Rich text editor unavailable: ${key}`);
    return { edit, el, editor };
  });
  for (const { edit, el, editor } of targets) {
    if (edit.field === 'insurance_table') {
      for (const [key,section] of [['headers','thead'],['rows','tbody']]) tableRows(section).forEach((row,i) => cells(row).forEach((cell,j) => {
        const c = cellControls(cell), v = edit.after[key][i][j];
        if (c.checkbox) { set(c.checkbox, v[0]); set(c.text, v[1]); } else set(c.text, v);
      }));
    } else if (editor) {
      // setData is asynchronous; the caller waits for editor values before Save.
      editor.setData(edit.after); editor.updateElement();
    } else set(el, el.tagName === 'SELECT' ? edit.after.value : edit.after);
  }
  const values = {};
  for (const key of keys) {
    const el = control(key), editor = window.CKEDITOR?.instances?.[key];
    values[key] = el.tagName === 'SELECT' ? { value: el.value, text: el.selectedOptions[0]?.textContent.trim() || '' } : editor ? editor.getData() : el.value;
  }
  const featured = form.querySelector('.img_change')?.getAttribute('src') || '';
  const gallery = [...form.querySelectorAll('.upload__img-box[data-id]')].map((box,i) => {
    const bg = box.querySelector('.img-bg')?.style.backgroundImage || '';
    const url = /^url\(["']?(.*?)["']?\)$/.exec(bg)?.[1];
    if (!url) throw new Error('Cannot read gallery image URL.');
    return { position: i + 1, id: box.dataset.id, url };
  });
  const hidden = {};
  for (const key of ['vehicle_id','count_image_old','is_main_image_id','_token']) {
    const el = form.querySelector(`[name="${key}"]`);
    if (el) hidden[key] = el.value;
  }
  hidden._token = hidden._token || document.querySelector('meta[name="csrf-token"]')?.content || '';
  return { fields: values, insurance_table: readTable(), images: { featured, gallery }, hidden };
}
async function targetEditor(page, id, timeout = 60000) {
  await page.goto(`${SITE}/vehicles/detail/${id}`, { waitUntil: 'load', timeout });
  if (page.url() !== `${SITE}/vehicles/detail/${id}`) throw new Error(`Backend vehicle ${id} did not open. Log in to admin.motorhomehire.com in the configured Hidemium profile.`);
  await page.locator('#submit_ajax_vehicle #name_vehicle').waitFor({ state: 'visible', timeout });
  await page.waitForFunction(() => ['top_insurance','terms_conditions'].every(k => {
    const el = document.querySelector(`#${k}`);
    const editor = window.CKEDITOR?.instances?.[k];
    return el && editor?.status === 'ready' && (!el.value.trim() || editor.getData().trim());
  }), null, { timeout });
  const idInput = page.locator('#submit_ajax_vehicle [name="vehicle_id"]');
  if (await idInput.count() && await idInput.inputValue() !== String(id)) throw new Error('Live backend vehicle ID mismatch.');
  const view = await page.evaluate(() => ({ lang: document.querySelector('#lang')?.value, country: document.querySelector('#country')?.value }));
  if (view.lang !== 'en' || view.country !== 'au') throw new Error('Expected the English/Australia backend view used by these snapshots.');
}
const readForm = page => page.evaluate(formOperation, { keys: editable });
function checkLive(live, vehicle, desired = false) {
  const expected = desired ? vehicle.proposed : vehicle.baseline;
  const operation = new Map(vehicle.operations.map(o => [o.field, o]));
  for (const key of editable) {
    const edit = operation.get(`fields.${key}`), value = live.fields[key];
    if (!sameField(key, value, expected.fields[key]) && !(edit && !desired && sameField(key, value, edit.after))) throw new Error(`Live ${key} differs from the reviewed snapshot. Export current and regenerate the report.`);
  }
  if (!equal(live.insurance_table, expected.insurance_table) && !(!desired && operation.has('insurance_table') && equal(live.insurance_table, vehicle.proposed.insurance_table))) throw new Error('Live insurance table differs from the reviewed snapshot.');
  if (!equal(live.images, expected.images)) throw new Error('Live images differ from the reviewed snapshot. Export current and regenerate the report.');
  for (const key of ['vehicle_id','count_image_old','is_main_image_id']) if (live.hidden[key] !== undefined && live.hidden[key] !== expected.fields[key]) throw new Error(`Live system field changed: ${key}`);
}
function snapshot(live, baseline) {
  for (const key of ['vehicle_id','count_image_old','is_main_image_id','_token']) {
    if (typeof live.hidden[key] !== 'string' || !live.hidden[key]) throw new Error(`Cannot export backend profile: live ${key} is missing.`);
  }
  if (live.hidden.vehicle_id !== baseline.source.vehicle_id) throw new Error('Cannot export a different backend vehicle identity.');
  if (live.hidden.count_image_old !== String(live.images.gallery.length)) throw new Error('Live image count does not match the gallery.');
  return validate({
    schema_version: 1,
    source: { vehicle_id: live.hidden.vehicle_id, url: `${SITE}/vehicles/detail/${live.hidden.vehicle_id}`, exported_at: new Date().toISOString() },
    images: structuredClone(live.images),
    fields: { ...structuredClone(live.hidden), ...structuredClone(live.fields) },
    insurance_table: structuredClone(live.insurance_table),
  });
}
async function processVehicle(page, vehicle, { timeout = 60000, store } = {}) {
  if (typeof store !== 'function') throw new Error('The catalog capture callback is required.');
  if (vehicle.operations.some(edit => !updateFields.some(key => edit.field === `fields.${key}`))) throw new Error('Only detail_link, age_detail, and age_vehicle may be updated.');
  const id = vehicle.baseline.source.vehicle_id;
  await targetEditor(page, id, timeout);
  const before = await readForm(page); checkLive(before, vehicle);
  const edits = vehicle.operations.filter(o => o.field === 'insurance_table' ? !equal(before.insurance_table, o.after) : !sameField(o.field.slice(7), before.fields[o.field.slice(7)], o.after));
  if (edits.length) {
    await page.evaluate(formOperation, { keys: editable, edits });
    await page.waitForFunction(operations => operations.filter(o => ['fields.top_insurance','fields.terms_conditions'].includes(o.field)).every(o => {
      const key = o.field.slice(7), editor = window.CKEDITOR?.instances?.[key];
      const same = value => value.replace(/\r\n?/g, '\n').trimEnd() === o.after.replace(/\r\n?/g, '\n').trimEnd();
      if (editor) { editor.updateElement(); return same(editor.getData()) && same(document.querySelector(`[name="${key}"]`).value); }
      return same(document.querySelector(`[name="${key}"]`).value);
    }), edits, { timeout });
    checkLive(await readForm(page), vehicle, true);
    const validation = await page.evaluate(() => {
      const form = document.querySelector('#submit_ajax_vehicle');
      if (window.jQuery?.fn?.valid && !window.jQuery(form).valid()) return [...form.querySelectorAll('label.error')].map(el => el.textContent).join('; ') || 'Backend form validation failed.';
      return form.checkValidity() ? '' : 'Backend input constraints rejected a proposed value.';
    });
    if (validation) throw new Error(validation);
    const responseWait = page.waitForResponse(r => {
      const u = new URL(r.url()); return u.origin === SITE && u.pathname === '/vehicles/store' && r.request().method() === 'POST';
    }, { timeout }).catch(() => null);
    log(`Backend ${id}: saving ${edits.length} changes.`);
    await page.locator('#saveDataVehicle').click();
    log(`Backend ${id}: waiting 5 seconds after pressing Save/Update.`);
    await new Promise(resolve => setTimeout(resolve, 5000));
    const response = await responseWait;
    if (!response) throw new Error('No backend save response was detected. Inspect the live save handler; do not assume this run saved.');
    if (!response.ok()) throw new Error(`Backend save returned HTTP ${response.status()}.`);
    const body = await response.json().catch(() => null);
    if (body && (body.success === false || body.result === false || ['error','failed'].includes(body.status))) throw new Error('Backend rejected the save. Inspect its validation errors.');
    await targetEditor(page, id, timeout);
  }
  const after = await readForm(page); checkLive(after, vehicle, true);
  const current = snapshot(after, vehicle.baseline);
  await store(current, vehicle);
  return { postId: vehicle.postId, filename: vehicle.filename, currentFilename: vehicle.filename, currentHash: fingerprint(current), status: edits.length ? 'saved' : 'unchanged' };
}

module.exports = { formOperation, targetEditor, readForm, checkLive, snapshot, processVehicle };
