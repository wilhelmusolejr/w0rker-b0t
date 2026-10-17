const fs = require('node:fs');
const path = require('node:path');
const { randomUUID } = require('node:crypto');
const { countrySite, countryForSite, SCHEMA_VERSION } = require('./campervanfinder-schema.cjs');

// Shared by the single-product exporter and the future list downloader.
// Offline mode is only for analysing saved references; it marks the output sampleOnly.
function captureProduct({ postId, editUrl, offline = false, capturedAt, sourceFile, siteUrl, schemaVersion }) {
  const nullable = value => value == null || String(value).trim() === '' ? null : String(value).replace(/\r\n?/g, '\n');
  const positive = value => /^[1-9]\d*$/.test(String(value || '')) && Number.isSafeInteger(Number(value)) ? Number(value) : null;
  const one = selector => {
    const nodes = document.querySelectorAll(selector);
    if (nodes.length !== 1) throw new Error(`Expected one product control: ${selector}`);
    return nodes[0];
  };
  const value = selector => {
    // Fresh editor loads expose the saved HTML source in the textarea. Reading
    // TinyMCE would reserialize tags/entities and introduce artificial differences.
    return nullable(one(selector).value);
  };
  if (!offline) {
    const url = new URL(location.href);
    if (url.origin !== siteUrl || url.pathname !== '/wp-admin/post.php' || url.searchParams.get('post') !== String(postId) || one('[name="post_ID"]').value !== String(postId) || one('[name="post_type"]').value !== 'product') throw new Error('Unexpected product identity during capture.');
  }
  const tabs = {}, tabLabels = {};
  for (const box of document.querySelectorAll('.woocommerce_product_tab')) {
    const title = box.querySelector('input[name^="product_tab_title["]');
    const match = /^product_tab_title\[(\d+)\]$/.exec(title?.name || '');
    if (!match) throw new Error('Could not identify a product tab form index.');
    const index = match[1];
    if (tabs[index]) throw new Error(`Duplicate tab index ${index}.`);
    const classes = [...box.classList];
    const custom = classes.map(c => /^product_tab_(\d+)$/.exec(c)).find(Boolean);
    const core = classes.find(c => c.startsWith('core_tab_'));
    if (!custom && !core) throw new Error(`Unknown tab identity ${index}.`);
    const tab = { tabId: custom ? Number(custom[1]) : core.slice(9), kind: custom ? 'custom' : 'core', position: Number(box.getAttribute('rel')), title: nullable(title.value) };
    const heading = box.querySelector(`[name="product_tab_heading[${index}]"]`);
    const content = box.querySelector(`[name="product_tab_content[${index}]"]`);
    if (heading) tab.heading = nullable(heading.value);
    if (content) tab.content = value(`#${content.id}`);
    tabs[index] = tab;
    tabLabels[index] = box.querySelector('.product_tab_name')?.textContent.trim() || tab.title;
  }
  const categories = {}, categoryLabels = {};
  for (const el of document.querySelectorAll('#product_catchecklist input[name="tax_input[product_cat][]"]')) {
    const id = positive(el.value);
    if (!id || Object.hasOwn(categories, id)) throw new Error('Invalid or duplicate category ID.');
    categories[id] = el.checked;
    categoryLabels[id] = el.closest('label')?.textContent.replace(/\s+/g, ' ').trim() || null;
  }
  if (!Object.keys(categories).length) throw new Error('Product category checklist missing.');
  const images = [...document.querySelectorAll('#woocommerce-product-images li.image[data-attachment_id]')].map((el, index) => ({ attachmentId: positive(el.dataset.attachment_id), position: index + 1 }));
  if (images.some(image => !image.attachmentId)) throw new Error('Invalid gallery attachment ID.');
  // Thumbnail addresses only help the Review hub show the images; they are audit data, not edited.
  const galleryPreviews = [...document.querySelectorAll('#woocommerce-product-images li.image[data-attachment_id]')].map(el => ({ attachmentId: positive(el.dataset.attachment_id), url: nullable(el.querySelector('img')?.getAttribute('src')) })).filter(item => item.url && /^https?:/i.test(item.url));
  const thumbnail = document.querySelector('#_thumbnail_id, input[name="_thumbnail_id"]');
  let featuredId = positive(thumbnail?.value);
  let featuredImageIdSource = thumbnail ? 'hidden_input' : 'unavailable';
  if (!thumbnail && !offline) {
    const settings = window.wp?.media?.view?.settings?.post || window._wpMediaViewsL10n?.settings?.post;
    if (settings && (settings.id == null || Number(settings.id) === postId) && Object.prototype.hasOwnProperty.call(settings, 'featuredImageId')) {
      featuredId = positive(settings.featuredImageId);
      featuredImageIdSource = 'wordpress_media_settings';
    }
  }
  const preview = document.querySelector('#postimagediv img');
  const warnings = [];
  if (offline) warnings.push('The saved reference does not expose a featured-image attachment ID. Capture a live product before generating an edit report.');
  else if (featuredImageIdSource === 'unavailable' && preview) warnings.push('This legacy featured-image box does not expose the attachment ID. attachmentId is unknown (null); the image preview is retained in audit.featuredImagePreview.');
  const commerce = {};
  for (const el of document.querySelectorAll('#woocommerce-product-data input[name], #woocommerce-product-data select[name], #woocommerce-product-data textarea[name]')) {
    if (!/^(?:product-type|_(?:virtual|downloadable|sku|product_url|button_text|regular_price|sale_price|sale_price_dates_from|sale_price_dates_to|download_limit|download_expiry|download_type|manage_stock|stock|backorders|stock_status|sold_individually|weight|length|width|height|purchase_note)|product_shipping_class|menu_order|comment_status)$/.test(el.name)) continue;
    commerce[el.name] = el.type === 'checkbox' ? el.checked : nullable(el.value);
  }
  const permalink = document.querySelector('#sample-permalink');
  const data = {
    vehicle: { name: value('#title') },
    descriptionAndImages: { description: value('#content'), short_description: value('#excerpt'), featured_image: { attachmentId: featuredId }, image_gallery: { image_count: images.length, images } },
    productTabs: { override_layout: one('#_override_tab_layout').checked, tabs },
    taxonomies: { product_categories: categories, product_tags: value('#tax-input-product_tag') },
  };
  if (!data.vehicle.name) throw new Error('Cannot capture a blank product title.');
  return {
    schemaVersion, bundleType: 'campervanfinder_vehicle_profile_snapshot',
    capture: { capturedAt: capturedAt || new Date().toISOString(), source: offline ? 'saved_wordpress_product_editor_mhtml_sample' : 'live_wordpress_product_editor', siteUrl, editUrl, postId, postType: 'product', postStatus: document.querySelector('#post_status')?.value || null, locale: document.documentElement.lang || null, slug: nullable(document.querySelector('#post_name')?.value || document.querySelector('#editable-post-name-full')?.textContent), publicUrl: nullable(permalink?.querySelector('a')?.href || permalink?.textContent) },
    data,
    audit: { sampleOnly: offline, ...(sourceFile ? { sourceFile } : {}), categoryLabels, tabLabels, productData: commerce, featuredImageIdSource, galleryPreviews, featuredImagePreview: preview ? { url: preview.src, altText: preview.alt, displayWidth: preview.width, displayHeight: preview.height } : null, warnings },
  };
}

async function captureProfile(page, postId, { country = 'au', ...options } = {}) {
  return page.evaluate(captureProduct, { postId, editUrl: page.url(), ...options, siteUrl: countrySite(country).origin, schemaVersion: SCHEMA_VERSION });
}

function writeCurrent(snapshot, workspace) {
  const { validateSnapshot } = require('./campervanfinder-schema.cjs');
  const country = countryForSite(snapshot.capture?.siteUrl);
  validateSnapshot(snapshot, { country });
  const folder = path.join(workspace, 'current');
  fs.mkdirSync(folder, { recursive: true });
  const name = snapshot.data.vehicle.name.replace(/[<>:"/\\|?*\x00-\x1f]/g, '-').replace(/[. ]+$/g, '').slice(0, 140).replace(/[. ]+$/g, '') || 'vehicle';
  const filename = `${snapshot.capture.postId} - ${country} - ${name}.json`;
  const target = path.join(folder, filename), temp = `${target}.${randomUUID()}.tmp`;
  try { fs.writeFileSync(temp, JSON.stringify(snapshot, null, 2) + '\n', 'utf8'); fs.renameSync(temp, target); }
  finally { if (fs.existsSync(temp)) fs.unlinkSync(temp); }
  for (const old of fs.readdirSync(folder)) {
    if (old !== filename && old.startsWith(`${snapshot.capture.postId} - ${country} - `) && old.endsWith('.json')) fs.unlinkSync(path.join(folder, old));
  }
  return { filename, snapshot };
}

module.exports = { captureProduct, captureProfile, writeCurrent };
