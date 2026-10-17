const fs = require('node:fs');
const path = require('node:path');
const { randomUUID } = require('node:crypto');
const { captureFolder } = require('./project.cjs');
const { fieldMappings, specialFeatureLabels } = require('./motorhomehire-profile-schema.cjs');

// Runs in the authenticated editor after a fresh load. Reads DOM and media only.
async function captureVehicleProfile({ fields, specialFeatureLabels, expectedPostId }) {
  const blankFields = [], warnings = [];
  const nullable = value => value == null || String(value).trim() === '' ? null : String(value);
  const number = value => Number.isSafeInteger(Number(value)) && Number(value) > 0 ? Number(value) : null;
  const inputValue = name => document.querySelector(`[name="${name}"]`)?.value;
  const actualId = number(inputValue('post_ID'));
  if (actualId !== expectedPostId || inputValue('post_type') !== 'vehicle') throw new Error('Unexpected vehicle identity during export.');
  const editor = new URL(location.href);
  if (editor.origin !== 'https://motorhomehire.com' || editor.pathname !== '/wp-admin/post.php' || editor.searchParams.get('post') !== String(expectedPostId)) throw new Error('Export is not on the expected vehicle editor.');
  const data = {};
  const set = (jsonPath, value) => {
    const keys = jsonPath.split('.').slice(1);
    let container = data;
    for (const key of keys.slice(0, -1)) container = container[key] ||= {};
    container[keys.at(-1)] = value;
    if (value === null) blankFields.push(jsonPath);
  };
  for (const field of fields) {
    if (!['text', 'textarea', 'select', 'checkbox'].includes(field.type)) continue;
    const element = document.querySelector(field.selector);
    if (!element) throw new Error(`Export control missing: ${field.jsonPath}`);
    let value;
    if (field.type === 'checkbox') {
      const choices = [...element.querySelectorAll('input[type="checkbox"]')];
      const selected = choices.filter(input => input.checked);
      if (choices.length === 1) value = choices[0].checked;
      else {
        const meanings = choices.map(input => input.closest('label')?.textContent.trim().toLowerCase());
        if (choices.length !== 2 || !meanings.includes('yes') || !meanings.includes('no')) throw new Error(`Unexpected checkbox choices: ${field.jsonPath}`);
        value = selected.length === 1 ? selected[0].closest('label').textContent.trim().toLowerCase() === 'yes' : null;
        if (value === null) warnings.push(`${field.jsonPath}: neither or both Yes/No choices selected.`);
      }
    } else {
      const control = field.selector === '#title' ? element : element.querySelector(field.type === 'textarea' ? 'textarea' : field.type === 'select' ? 'select' : 'input[type="text"]');
      if (!control) throw new Error(`Export input missing: ${field.jsonPath}`);
      value = nullable(control.value);
    }
    set(field.jsonPath, value);
  }
  if (!data.vehicle.name) throw new Error('Cannot export a vehicle with a blank name.');
  const taxonomy = name => {
    const checklist = document.getElementById(`${name}checklist`);
    if (!checklist) throw new Error(`Export taxonomy missing: ${name}`);
    return [...checklist.querySelectorAll('input[type="checkbox"]')].filter(input => input.name === `tax_input[${name}][]`).map(input => ({
      termId: number(input.value), label: input.closest('label')?.textContent.replace(/\s+/g, ' ').trim(), selected: input.checked,
    }));
  };
  const brands = taxonomy('brand'), locations = taxonomy('mh_location'), features = taxonomy('special-features');
  const selectedBrands = brands.filter(term => term.selected);
  if (selectedBrands.length > 1) throw new Error('Export found multiple brands; a profile requires one brand name.');
  data.taxonomies = {
    brand: selectedBrands[0]?.label || null,
    rental_locations: locations.filter(term => term.selected).map(term => term.label),
    special_features: {},
  };
  if (!data.taxonomies.brand) blankFields.push('data.taxonomies.brand');
  if (!data.taxonomies.rental_locations.length) blankFields.push('data.taxonomies.rental_locations');
  for (const [key, label] of Object.entries(specialFeatureLabels)) {
    const term = features.find(term => term.label === label);
    if (!term) throw new Error(`Export special feature missing: ${label}`);
    data.taxonomies.special_features[key] = term.selected;
  }

  const emptyText = value => {
    if (value && typeof value === 'object') value = value.raw ?? value.rendered;
    if (value == null) return null;
    const div = document.createElement('div');
    div.innerHTML = String(value);
    return nullable(div.textContent);
  };
  const date = value => value instanceof Date ? value.toISOString() : nullable(value);
  const bytesLabel = value => {
    if (!number(value)) return null;
    let amount = Number(value), unit = 0;
    while (amount >= 1024 && unit < 3) { amount /= 1024; unit++; }
    return amount.toFixed(amount >= 10 || unit === 0 ? 0 : 1) + ' ' + ['B', 'KB', 'MB', 'GB'][unit];
  };
  const filename = url => url ? decodeURIComponent(new URL(url, location.origin).pathname.split('/').at(-1)) : null;
  const cache = new Map();
  const metadata = async attachmentId => {
    if (cache.has(attachmentId)) return cache.get(attachmentId);
    const load = async () => {
      let attributes, modelError;
      try {
        const model = window.wp?.media?.attachment?.(attachmentId);
        if (!model) throw new Error('WordPress attachment model unavailable.');
        await new Promise((resolve, reject) => {
          const timer = setTimeout(() => reject(new Error('Media model request timed out.')), 15_000);
          const done = callback => (...args) => { clearTimeout(timer); callback(...args); };
          try { model.fetch({ success: done(resolve), error: done((unused, response) => reject(new Error(response?.statusText || 'Attachment request failed.'))) }); }
          catch (error) { clearTimeout(timer); reject(error); }
        });
        attributes = model.toJSON();
        if (Number(attributes.id) !== attachmentId) throw new Error('Media model returned the wrong attachment identity.');
        if (!attributes.url || !attributes.width || !attributes.height || !attributes.date) throw new Error('Media model is missing original image metadata.');
      } catch (error) { modelError = error; }
      let record;
      if (!modelError) {
        record = {
          attachmentId, filename: nullable(attributes.filename) || filename(attributes.url),
          uploadedAt: date(attributes.date), uploadedAtGmt: date(attributes.date_gmt || attributes.dateGmt),
          uploadedDateDisplay: nullable(attributes.dateFormatted), modifiedAt: date(attributes.modified),
          width: number(attributes.width), height: number(attributes.height), fileSizeBytes: number(attributes.filesizeInBytes),
          fileSizeHuman: nullable(attributes.filesizeHumanReadable) || bytesLabel(attributes.filesizeInBytes), mimeType: nullable(attributes.mime),
          url: attributes.url, thumbnailUrl: attributes.sizes?.thumbnail?.url || attributes.sizes?.medium?.url || null,
          title: emptyText(attributes.title), altText: nullable(attributes.alt), caption: emptyText(attributes.caption),
          description: emptyText(attributes.description), metadataSource: 'wordpress_media_model',
        };
      } else {
        const apiRoot = window.wpApiSettings?.root || new URL('/wp-json/', location.origin).href;
        const url = new URL(`wp/v2/media/${attachmentId}`, apiRoot.endsWith('/') ? apiRoot : apiRoot + '/');
        if (url.origin !== location.origin) throw new Error('Unexpected media API origin.');
        const nonce = window.wpApiSettings?.nonce;
        const response = await fetch(url, { credentials: 'same-origin', headers: nonce ? { 'X-WP-Nonce': nonce } : {}, signal: AbortSignal.timeout(15_000) });
        if (!response.ok) throw new Error(`Attachment ${attachmentId}: media model failed (${modelError.message}); REST returned HTTP ${response.status}.`);
        const media = await response.json();
        if (media.id !== attachmentId) throw new Error(`Wrong media identity for attachment ${attachmentId}.`);
        const details = media.media_details || {};
        record = {
          attachmentId, filename: filename(media.source_url), uploadedAt: date(media.date), uploadedAtGmt: date(media.date_gmt),
          uploadedDateDisplay: null, modifiedAt: date(media.modified), width: number(details.width), height: number(details.height),
          fileSizeBytes: number(details.filesize || details.filesize_bytes), fileSizeHuman: bytesLabel(details.filesize || details.filesize_bytes),
          mimeType: nullable(media.mime_type), url: media.source_url, thumbnailUrl: details.sizes?.thumbnail?.source_url || details.sizes?.medium?.source_url || null,
          title: emptyText(media.title), altText: nullable(media.alt_text), caption: emptyText(media.caption), description: emptyText(media.description),
          metadataSource: 'wordpress_rest_api',
        };
      }
      if (!record.filename || !record.width || !record.height || !record.uploadedAt) throw new Error(`Attachment ${attachmentId}: incomplete fresh media metadata; current snapshot was not replaced.`);
      return { ...record, dimensions: `${record.width} x ${record.height}`, metadataComplete: true, metadataStatus: 'complete', metadataWarnings: [] };
    };
    const pending = load();
    cache.set(attachmentId, pending);
    return pending;
  };
  const galleryField = document.querySelector('.acf-field[data-name="image_gallery"]');
  if (!galleryField) throw new Error('Export gallery control missing.');
  const galleryItems = [...galleryField.querySelectorAll('.acf-gallery-attachments > .acf-gallery-attachment[data-id]')];
  const galleryIds = galleryItems.map(item => number(item.dataset.id));
  if (galleryIds.some(id => !id) || new Set(galleryIds).size !== galleryIds.length) throw new Error('Invalid or duplicate gallery attachment IDs.');
  const images = new Array(galleryIds.length);
  let next = 0;
  await Promise.all(Array.from({ length: Math.min(4, galleryIds.length) }, async () => {
    while (next < galleryIds.length) {
      const index = next++;
      images[index] = { position: index + 1, role: 'gallery', ...await metadata(galleryIds[index]) };
    }
  }));
  const preview = document.querySelector('#postimagediv #set-post-thumbnail img');
  const featuredValue = inputValue('_thumbnail_id');
  const featuredId = featuredValue !== undefined ? number(featuredValue) : number(window.wp?.media?.view?.settings?.post?.featuredImageId) || number(window._wpMediaViewsL10n?.settings?.post?.featuredImageId);
  if (preview && !featuredId) throw new Error('Featured image is visible but its attachment ID could not be exported.');
  let featuredImage = null;
  if (featuredId) {
    featuredImage = {
      role: 'featured_image', ...await metadata(featuredId),
      displayUrl: preview?.getAttribute('src') || null, displayAltText: nullable(preview?.getAttribute('alt')),
      displayWidth: number(preview?.getAttribute('width')), displayHeight: number(preview?.getAttribute('height')),
    };
    const galleryIndex = galleryIds.indexOf(featuredId);
    if (galleryIndex >= 0) featuredImage.galleryPosition = galleryIndex + 1;
  }
  data.descriptionAndImages.featured_image = featuredImage;
  data.descriptionAndImages.image_gallery = { image_count: images.length, images };
  const month = document.querySelector('#mm')?.value, day = document.querySelector('#jj')?.value, year = document.querySelector('#aa')?.value;
  const pad = (value, length = 2) => String(value || '0').padStart(length, '0');
  const postDate = month && day && year ? `${pad(year, 4)}-${pad(month)}-${pad(day)}T${pad(document.querySelector('#hh')?.value)}:${pad(document.querySelector('#mn')?.value)}:00` : null;
  const publicUrl = document.querySelector('#sample-permalink a')?.href || document.querySelector('#view-post-btn a')?.href || null;
  const configuredFields = fields.filter(field => ['text', 'textarea', 'select', 'checkbox'].includes(field.type)).length + 2 + Object.keys(specialFeatureLabels).length;
  const mediaCount = images.length + (featuredImage ? 1 : 0);
  return {
    schemaVersion: '1.1', bundleType: 'vehicle_profile_snapshot',
    capture: { capturedAt: new Date().toISOString(), source: 'current_wordpress_vehicle_editor', siteUrl: editor.origin, editUrl: editor.href,
      postId: actualId, postType: 'vehicle', postStatus: inputValue('original_post_status') || inputValue('post_status') || null,
      language: editor.searchParams.get('lang') || document.documentElement.lang || null, locale: document.documentElement.lang || null,
      slug: nullable(inputValue('post_name')), publicUrl, postDate },
    data,
    audit: {
      completeness: { configuredFields, controlsFound: configuredFields, populatedFields: configuredFields - blankFields.length, blankFields: blankFields.length, missingControls: 0 },
      blankFields, missingControls: [], readErrors: [], selectedSpecialFeatures: Object.keys(data.taxonomies.special_features).filter(key => data.taxonomies.special_features[key]),
      images: { featuredImagePresent: !!featuredImage, galleryImageCount: images.length, uniqueAttachmentCount: new Set([...galleryIds, ...(featuredImage ? [featuredId] : [])]).size, completeMetadataCount: mediaCount, incompleteMetadataCount: 0 },
      warnings,
    },
  };
}

function vehicleFilename(profile, fallbackCountry) {
  const id = profile.capture?.postId;
  if (!Number.isSafeInteger(id) || id <= 0 || profile.capture.postType !== 'vehicle') throw new Error('Snapshot vehicle identity is invalid.');
  const url = profile.capture.publicUrl ? new URL(profile.capture.publicUrl) : null;
  if (url && url.origin !== 'https://motorhomehire.com') throw new Error('Snapshot public URL has an unexpected origin.');
  const countryFromUrl = url?.pathname.startsWith('/australia/') ? 'au' : url?.pathname.startsWith('/new-zealand/') ? 'nz' : null;
  const country = countryFromUrl || fallbackCountry;
  if (!['au', 'nz'].includes(country)) throw new Error('Could not determine AU/NZ for the snapshot filename.');
  if (countryFromUrl && fallbackCountry && countryFromUrl !== fallbackCountry) throw new Error('Saved vehicle country does not match the selected baseline.');
  const name = profile.data?.vehicle?.name;
  if (typeof name !== 'string' || !name.trim()) throw new Error('Snapshot vehicle name is blank.');
  const safeName = name.trim().replace(/[<>:"/\\|?*\x00-\x1f]/g, '-').replace(/[. ]+$/g, '');
  if (!safeName) throw new Error('Vehicle name cannot form a valid filename.');
  const filename = `${id} - ${country} - ${safeName}.json`;
  if (filename.length > 240) throw new Error('Vehicle filename is too long.');
  return filename;
}

function storeCurrentSnapshot(profile, vehicle, workspace = captureFolder('motorhomehireProfile')) {
  if (profile.schemaVersion !== '1.1' || profile.bundleType !== 'vehicle_profile_snapshot' || !profile.audit || !profile.data?.descriptionAndImages?.image_gallery) throw new Error('Incomplete vehicle snapshot; current JSON was not replaced.');
  if (profile.capture?.postId !== Number(vehicle.postId)) throw new Error('Exported snapshot is for a different vehicle.');
  const fallbackCountry = /^(\d+) - (au|nz) - .+\.json$/.exec(vehicle.filename || '')?.[2] || vehicle.country;
  const filename = vehicleFilename(profile, fallbackCountry);
  const directory = path.resolve(workspace, 'current');
  fs.mkdirSync(directory, { recursive: true });
  const destination = path.resolve(directory, filename);
  if (path.dirname(destination) !== directory) throw new Error('Snapshot destination is outside current.');
  const oldNames = fs.readdirSync(directory).filter(name => new RegExp(`^${vehicle.postId} - (au|nz) - .+\\.json$`).test(name));
  if (oldNames.length > 1) throw new Error(`More than one current JSON exists for vehicle ${vehicle.postId}.`);
  for (const oldName of oldNames) {
    const file = path.resolve(directory, oldName);
    if (path.dirname(file) !== directory || !fs.lstatSync(file).isFile()) throw new Error('Current baseline is not a regular vehicle JSON file.');
    const old = JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
    if (old.capture?.postId !== Number(vehicle.postId)) throw new Error('Current baseline filename and vehicle identity disagree.');
  }
  const temporary = `${destination}.${randomUUID()}.tmp`;
  try {
    fs.writeFileSync(temporary, JSON.stringify(profile, null, 2) + '\n', { encoding: 'utf8', flag: 'wx' });
    fs.renameSync(temporary, destination);
  } finally { if (fs.existsSync(temporary)) fs.unlinkSync(temporary); }
  for (const oldName of oldNames) if (oldName !== filename) fs.unlinkSync(path.join(directory, oldName));
  return { filename, file: destination };
}

async function exportCurrentVehicle(page, vehicle, workspace = captureFolder('motorhomehireProfile')) {
  const profile = await page.evaluate(captureVehicleProfile, { fields: fieldMappings, specialFeatureLabels, expectedPostId: Number(vehicle.postId) });
  return storeCurrentSnapshot(profile, vehicle, workspace);
}

module.exports = { captureVehicleProfile, vehicleFilename, storeCurrentSnapshot, exportCurrentVehicle };
