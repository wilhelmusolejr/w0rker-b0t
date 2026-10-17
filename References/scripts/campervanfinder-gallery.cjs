const fs = require('node:fs');
const store = require('./catalog-store.cjs');
const gallery = require('./catalog-gallery.cjs');

const BOX = '#woocommerce-product-images';
const LIST = `${BOX} ul.product_images`;

// Runs in the editor. Check the real WooCommerce field and sortable callback,
// including on NZ, before clicking any gallery removal control.
function inspectGallery({ postId }) {
  if (document.querySelector('[name="post_ID"]')?.value !== String(postId) || document.querySelector('[name="post_type"]')?.value !== 'product') throw new Error('Unexpected gallery product identity.');
  const lists = document.querySelectorAll('#woocommerce-product-images ul.product_images');
  const inputs = document.querySelectorAll('input#product_image_gallery[name="product_image_gallery"]');
  const add = document.querySelectorAll('#woocommerce-product-images .add_product_images a');
  if (lists.length !== 1 || inputs.length !== 1 || inputs[0].disabled || add.length !== 1) throw new Error('The WooCommerce gallery controls are missing or ambiguous.');
  const images = [...lists[0].querySelectorAll(':scope > li.image[data-attachment_id]')];
  const ids = images.map(image => Number(image.dataset.attachment_id));
  if (ids.some(id => !Number.isSafeInteger(id) || id < 1) || new Set(ids).size !== ids.length || images.some(image => image.querySelectorAll('a.delete').length !== 1)) throw new Error('Invalid gallery image or removal control.');
  const stored = inputs[0].value.split(',').filter(Boolean).map(Number);
  if (JSON.stringify(stored) !== JSON.stringify(ids)) throw new Error('Gallery images and their stored form field disagree.');
  const jq = window.jQuery;
  if (!jq?.fn?.sortable || typeof jq(lists[0]).sortable('option', 'update') !== 'function') throw new Error('The WooCommerce gallery sortable handler is unavailable.');
  if (typeof window.wp?.media?.ajax !== 'function' || typeof window.wp?.media?.attachment !== 'function') throw new Error('The WordPress media API is unavailable.');
  const settingsPost = window.wp?.media?.view?.settings?.post?.id;
  if (settingsPost != null && Number(settingsPost) !== Number(postId)) throw new Error('The media uploader belongs to another product.');
  const filters = window._wpPluploadSettings?.defaults?.filters;
  const entries = Array.isArray(filters) ? filters : filters?.mime_types || [];
  return { ids, allowedExtensions: entries.flatMap(entry => String(entry.extensions || '').toLowerCase().split(',')).filter(Boolean) };
}

function reorderGallery({ expected }) {
  const list = document.querySelector('#woocommerce-product-images ul.product_images');
  const nodes = [...list.querySelectorAll(':scope > li.image[data-attachment_id]')];
  const indexed = new Map(nodes.map(node => [Number(node.dataset.attachment_id), node]));
  if (indexed.size !== nodes.length || nodes.length !== expected.length || expected.some(id => !indexed.has(id))) throw new Error('Gallery membership changed before reordering.');
  if (JSON.stringify(nodes.map(node => Number(node.dataset.attachment_id))) === JSON.stringify(expected)) return false;
  const jq = window.jQuery, update = jq(list).sortable('option', 'update');
  if (typeof update !== 'function') throw new Error('The gallery sortable handler disappeared.');
  for (const id of expected) list.append(indexed.get(id));
  jq(list).sortable('refresh');
  update.call(list, jq.Event('sortupdate'), { item: jq(indexed.get(expected[0])) });
  jq(list).trigger('sortupdate');
  return true;
}

async function openMedia(page, timeout) {
  const add = page.locator(`${BOX} .add_product_images a`);
  if (!await add.isVisible()) await page.locator(`${BOX} .handlediv`).click({ timeout });
  await add.click({ timeout });
  const modal = page.locator('.media-modal:visible');
  await modal.waitFor({ state: 'visible', timeout });
  await modal.locator('.media-frame-router .media-menu-item').filter({ hasText: /^Upload Files$/i }).click({ timeout });
  const select = modal.locator('.browser').filter({ hasText: /^Select Files$/i });
  await select.waitFor({ state: 'visible', timeout });
  if (await modal.locator('.media-button-select').count() !== 1 || await modal.locator('.media-modal-close').count() !== 1) throw new Error('Unexpected WordPress gallery upload modal.');
  return { modal, select };
}

async function mediaList(page, postId, timeout = 300000) {
  const result = await page.evaluate(async ({ postId, timeout }) => {
    const deadline = Date.now() + timeout, result = [], seen = new Set();
    // Legacy wp.media.query returns a mirrored Attachments collection whose
    // inherited fetch() has no URL. Read the same authenticated query action
    // directly, with explicit pagination and no stale collection cache.
    for (let paged = 1; ; paged++) {
      const remaining = deadline - Date.now();
      if (remaining <= 0) throw new Error('Timed out reading product media.');
      let timer;
      const pending = window.wp.media.ajax({ data: { action: 'query-attachments', post_id: Number(postId), query: { post_parent: Number(postId), posts_per_page: 100, paged, orderby: 'ID', order: 'ASC' } } });
      let items;
      try { items = await Promise.race([Promise.resolve(pending), new Promise((_, reject) => { timer = setTimeout(() => { pending.abort?.(); reject(new Error('Timed out reading product media.')); }, remaining); })]); }
      catch { throw new Error('Could not read the product Media Library query.'); }
      finally { clearTimeout(timer); }
      if (!Array.isArray(items)) throw new Error('Invalid product media response.');
      for (const item of items) {
        const id = Number(item.id);
        if (seen.has(id)) throw new Error('Product media pagination repeated an attachment; retry the read.');
        seen.add(id);
        result.push({ id, url: item.url, type: item.type, uploadedTo: Number(item.uploadedTo) });
      }
      if (items.length < 100) return result;
    }
  }, { postId, timeout });
  if (result.some(item => !Number.isSafeInteger(item.id) || item.id < 1 || item.uploadedTo !== Number(postId))) throw new Error('Could not obtain a complete product media baseline for upload recovery.');
  return result;
}

async function attachment(page, id, timeout = 300000) {
  return page.evaluate(async ({ id, timeout }) => {
    const model = window.wp.media.attachment(id);
    let timer;
    const pending = model.fetch();
    try { await Promise.race([Promise.resolve(pending), new Promise((_, reject) => { timer = setTimeout(() => { pending.abort?.(); reject(new Error('Timed out reading uploaded image metadata.')); }, timeout); })]); }
    catch { throw new Error(`Could not read uploaded image metadata for ${id}.`); }
    finally { clearTimeout(timer); }
    const item = model.toJSON();
    return { id: Number(item.id), url: item.url, filename: item.filename, title: item.title, type: item.type, uploadedTo: Number(item.uploadedTo) };
  }, { id, timeout });
}

async function matchesAsset(page, media, asset, origin, timeout) {
  if (media.type !== 'image' || typeof media.url !== 'string') return false;
  const url = new URL(media.url);
  if (url.origin !== origin || !url.pathname.startsWith('/wp-content/uploads/')) throw new Error('Gallery attachment has an unexpected source URL.');
  const result = await page.evaluate(async ({ url, timeout }) => {
    const response = await fetch(url, { credentials: 'same-origin', cache: 'no-store', signal: AbortSignal.timeout(timeout) });
    if (!response.ok) throw new Error(`Uploaded image read returned HTTP ${response.status}.`);
    const bytes = await response.arrayBuffer();
    const digest = await crypto.subtle.digest('SHA-256', bytes);
    return { size: bytes.byteLength, sha256: [...new Uint8Array(digest)].map(byte => byte.toString(16).padStart(2, '0')).join('') };
  }, { url: url.href, timeout });
  return result.size === asset.size && result.sha256 === asset.sha256;
}

async function reconcileUpload(page, target, asset, entry, persist, timeout) {
  const origin = target.mapping.siteOrigin;
  if (entry.status === 'uploaded') {
    const media = await attachment(page, entry.attachmentId, timeout);
    if (media.id !== entry.attachmentId || media.uploadedTo !== Number(target.externalId) || !await matchesAsset(page, media, asset, origin, timeout)) throw new Error(`Previously uploaded image ${entry.attachmentId} no longer matches ${asset.filename}.`);
    return media;
  }
  if (entry.status !== 'submitting') return null;
  const candidates = (await mediaList(page, target.externalId, timeout)).filter(item => !entry.existingIds.includes(item.id));
  const matches = [];
  for (const item of candidates) if (await matchesAsset(page, item, asset, origin, timeout)) matches.push(item);
  if (matches.length !== 1) throw new Error(`Upload outcome is uncertain for ${asset.filename}: ${matches.length} matching new attachments. Reconcile this run before retrying; the file will not be uploaded again automatically.`);
  entry.status = 'uploaded'; entry.attachmentId = matches[0].id; persist();
  return attachment(page, entry.attachmentId, timeout);
}

async function clearSelection(modal, timeout) {
  const selected = modal.locator('.attachments .attachment.selected .check');
  while (await selected.count()) await selected.first().click({ timeout });
}

async function addUploaded(page, modal, id, { media, timeout }) {
  await modal.locator('.media-frame-router .media-menu-item').filter({ hasText: /^Media Library$/i }).click({ timeout });
  const tile = modal.locator(`.attachments .attachment[data-id="${id}"]`);
  if (!await tile.count() && media) {
    const search = modal.locator('.media-toolbar-primary input[type="search"]');
    await search.fill(String(media.title || media.filename).replace(/\.[^.]+$/, ''), { timeout });
    await search.press('Enter', { timeout });
  }
  await tile.waitFor({ state: 'visible', timeout });
  // Only the attachment confirmed for this file may be selected.
  for (const other of await modal.locator(`.attachments .attachment.selected:not([data-id="${id}"]) .check`).all()) await other.click({ timeout });
  if (!/(?:^|\s)selected(?:\s|$)/.test(await tile.getAttribute('class'))) await tile.click({ timeout });
  const selected = await modal.locator('.attachments .attachment.selected').evaluateAll(nodes => nodes.map(node => Number(node.getAttribute('data-id'))));
  if (!store.equal(selected, [id])) throw new Error('Media selection contains an unreviewed attachment.');
  await modal.locator('.media-button-select:not([disabled])').click({ timeout });
  await modal.waitFor({ state: 'hidden', timeout });
  await page.locator(`${LIST} > li.image[data-attachment_id="${id}"]`).waitFor({ state: 'attached', timeout });
}

async function uploadOne(page, target, asset, buffer, journal, persist, { timeout, log }) {
  let entry = journal.uploads.find(item => item.key === gallery.uploadKey(asset));
  let media = entry ? await reconcileUpload(page, target, asset, entry, persist, timeout) : null;
  const { modal, select } = await openMedia(page, timeout);
  await clearSelection(modal, timeout);
  if (!media) {
    const existingIds = (await mediaList(page, target.externalId, timeout)).map(item => item.id);
    const chooserResult = page.waitForEvent('filechooser', { timeout }).then(value => ({ value }), error => ({ error }));
    await select.click({ timeout });
    const chooser = await chooserResult;
    if (chooser.error) throw chooser.error;
    if (!entry) { entry = { key: gallery.uploadKey(asset), status: 'submitting', existingIds, attachmentId: null }; journal.uploads.push(entry); }
    else Object.assign(entry, { status: 'submitting', existingIds, attachmentId: null });
    persist(); // Persist intent before the irreversible Media Library upload.
    const responseResult = page.waitForResponse(response => {
      const url = new URL(response.url());
      return url.origin === target.mapping.siteOrigin && url.pathname === '/wp-admin/async-upload.php' && response.request().method() === 'POST';
    }, { timeout }).then(value => ({ value }), error => ({ error }));
    log(`Uploading ${asset.filename}.`);
    await chooser.value.setFiles({ name: asset.filename, mimeType: asset.mimeType, buffer }, { timeout });
    const result = await responseResult;
    if (result.error) throw result.error;
    let payload;
    try { payload = await result.value.json(); }
    catch { throw new Error(`Upload response for ${asset.filename} was not confirmed JSON. Resume this run to reconcile the uncertain attachment.`); }
    if (payload?.success === false) { entry.status = 'failed'; persist(); throw new Error(`WordPress rejected ${asset.filename}: ${payload.data?.message || 'upload failed'}`); }
    const id = Number(payload?.data?.id);
    if (!result.value.ok() || payload?.success !== true || !Number.isSafeInteger(id) || id < 1 || entry.existingIds.includes(id) || payload.data.type !== 'image') throw new Error(`Unconfirmed upload result for ${asset.filename}; resume this run before retrying.`);
    entry.status = 'uploaded'; entry.attachmentId = id; persist();
    media = { id, title: payload.data.title, filename: payload.data.filename };
  } else log(`Reusing confirmed upload ${entry.attachmentId} for ${asset.filename}.`);
  await addUploaded(page, modal, entry.attachmentId, { media, timeout });
}

async function applyGallery(page, target, context, { timeout = 300000, log = console.log } = {}) {
  if (!target.gallery) return { changed: false };
  const { root, journal, persistJournal } = context || {};
  if (!root || typeof persistJournal !== 'function') throw new Error('Gallery execution requires persistent run recovery state.');
  gallery.validateJournal(target, journal);
  const persist = () => { gallery.validateJournal(target, journal); persistJournal(journal); };
  const buffers = new Map();
  for (const asset of target.gallery.uploads) {
    const bytes = fs.readFileSync(store.inside(root, asset.path));
    if (bytes.length !== asset.size || gallery.digest(bytes) !== asset.sha256) throw new Error(`Gallery image changed since review: ${asset.filename}`);
    await page.evaluate(async ({ base64, type }) => {
      const bytes = Uint8Array.from(atob(base64), character => character.charCodeAt(0));
      const image = await createImageBitmap(new Blob([bytes], { type }));
      const valid = image.width > 0 && image.height > 0; image.close();
      if (!valid) throw new Error('Image cannot be decoded.');
    }, { base64: bytes.toString('base64'), type: asset.mimeType });
    buffers.set(gallery.uploadKey(asset), bytes);
  }
  const state = await page.evaluate(inspectGallery, { postId: target.externalId });
  for (const asset of target.gallery.uploads) if (state.allowedExtensions.length && !state.allowedExtensions.includes('*') && !state.allowedExtensions.includes(asset.filename.split('.').at(-1).toLowerCase())) throw new Error(`The site's uploader does not accept ${asset.filename}.`);
  // Reconcile prior submissions even if the final gallery is already saved.
  for (const asset of target.gallery.uploads) {
    const entry = journal.uploads.find(item => item.key === gallery.uploadKey(asset));
    if (entry && entry.status !== 'failed') await reconcileUpload(page, target, asset, entry, persist, timeout);
  }
  let expected = gallery.desiredIds(target, journal);
  if (expected && store.equal(state.ids, expected)) return { changed: false, expected };
  if (!store.equal(state.ids, target.gallery.baselineIds)) throw new Error('Live gallery conflict: the reviewed attachment IDs/order changed.');
  if (target.gallery.uploads.length) {
    const { modal } = await openMedia(page, timeout);
    await modal.locator('.media-modal-close').click({ timeout });
    await modal.waitFor({ state: 'hidden', timeout });
    // Check the product-specific media contract before removing any images.
    await mediaList(page, target.externalId, timeout);
  }
  const checked = await page.evaluate(inspectGallery, { postId: target.externalId });
  if (!store.equal(checked.ids, target.gallery.baselineIds)) throw new Error('Gallery changed during upload preflight.');
  for (const image of target.gallery.remove) {
    log(`Removing gallery position ${image.position} (attachment ${image.attachmentId}).`);
    const tile = page.locator(`${LIST} > li.image[data-attachment_id="${image.attachmentId}"]`);
    // The X shows only while hovered. A tile still sliding into place after the
    // previous removal can lose the hover, so hover and click again briefly.
    for (let attempt = 1; ; attempt++) {
      try {
        await tile.hover({ timeout });
        await tile.locator('a.delete').click({ timeout: Math.min(timeout, 5000) });
        break;
      } catch (error) {
        if (!(await tile.count())) break;
        if (attempt >= 6) throw error;
        log(`Gallery position ${image.position}: delete control not ready, retrying (${attempt}).`);
        await page.waitForTimeout(400);
      }
    }
    await tile.waitFor({ state: 'detached', timeout });
  }
  let current = await page.evaluate(inspectGallery, { postId: target.externalId });
  if (!store.equal(current.ids, target.gallery.retainedIds)) throw new Error('Gallery removal did not produce the reviewed retained images.');
  for (const asset of target.gallery.uploads) await uploadOne(page, target, asset, buffers.get(gallery.uploadKey(asset)), journal, persist, { timeout, log });
  expected = gallery.desiredIds(target, journal);
  await page.evaluate(reorderGallery, { expected });
  current = await page.evaluate(inspectGallery, { postId: target.externalId });
  if (!store.equal(current.ids, expected)) throw new Error('Gallery form value/order did not match the reviewed result.');
  return { changed: !store.equal(state.ids, expected), expected };
}

module.exports = { inspectGallery, reorderGallery, openMedia, mediaList, attachment, matchesAsset, reconcileUpload, applyGallery };
