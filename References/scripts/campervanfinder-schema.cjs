const SITE_URL = 'https://www.campervanfinder.com.au';
const COUNTRY_SITES = {
  au: { country: 'au', origin: SITE_URL, folder: 'captures/campervanfinder-au' },
  nz: { country: 'nz', origin: 'https://www.campervanfinder.co.nz', folder: 'captures/campervanfinder-nz' },
};
function countrySite(country = 'au') {
  if (!Object.hasOwn(COUNTRY_SITES, country)) throw new Error('Campervan Finder country must be au or nz.');
  return COUNTRY_SITES[country];
}
function countryForSite(origin) {
  const site = Object.values(COUNTRY_SITES).find(site => site.origin === origin);
  if (!site) throw new Error('Unsupported Campervan Finder website origin.');
  return site.country;
}
const SCHEMA_VERSION = '1.0';
const fieldMappings = [
  { jsonPath: 'data.vehicle.name', selector: '#title', type: 'text' },
  { jsonPath: 'data.descriptionAndImages.description', selector: '#content', type: 'html' },
  { jsonPath: 'data.descriptionAndImages.short_description', selector: '#excerpt', type: 'html' },
  { jsonPath: 'data.productTabs.override_layout', selector: '#_override_tab_layout', type: 'checkbox' },
];

function validateSnapshot(snapshot, { sample = false, country = 'au' } = {}) {
  const siteUrl = countrySite(country).origin;
  if (snapshot?.schemaVersion !== SCHEMA_VERSION || snapshot?.bundleType !== 'campervanfinder_vehicle_profile_snapshot') throw new Error('Expected a Campervan Finder schema 1.0 snapshot.');
  const capture = snapshot.capture;
  if (!Number.isSafeInteger(capture?.postId) || capture.postId <= 0 || capture.postType !== 'product' || capture.siteUrl !== siteUrl) throw new Error('Invalid Campervan Finder product identity.');
  const url = new URL(capture.editUrl);
  if (url.origin !== siteUrl || url.pathname !== '/wp-admin/post.php' || url.searchParams.get('post') !== String(capture.postId) || url.searchParams.get('action') !== 'edit') throw new Error('Invalid Campervan Finder editor URL.');
  if (!sample && (snapshot.audit?.sampleOnly || capture.source !== 'live_wordpress_product_editor')) throw new Error('Capture a live current JSON first; the saved-page sample is a reference only.');
  if (typeof snapshot.data?.vehicle?.name !== 'string' || !snapshot.data.vehicle.name.trim()) throw new Error('Vehicle name must be a nonempty string.');
  if (!snapshot.data.productTabs || typeof snapshot.data.productTabs.tabs !== 'object' || Array.isArray(snapshot.data.productTabs.tabs)) throw new Error('Expected productTabs.tabs keyed by form index.');
  return snapshot;
}

function resolveMapping(jsonPath, baseline) {
  const fixed = fieldMappings.find(field => field.jsonPath === jsonPath);
  if (fixed) return { ...fixed };
  const tab = /^data\.productTabs\.tabs\.(\d+)\.(title|heading|content)$/.exec(jsonPath);
  if (tab) {
    const [, index, property] = tab;
    const identity = baseline.data.productTabs.tabs[index];
    if (!identity || !Object.hasOwn(identity, property)) throw new Error(`Tab control is not in the baseline: ${jsonPath}`);
    return { jsonPath, selector: property === 'content' ? `#producttabcontent${index}` : `#product_tab_${property}_${index}`, type: property === 'content' ? 'html' : 'text', tabIndex: index, tabId: identity.tabId, kind: identity.kind };
  }
  const category = /^data\.taxonomies\.product_categories\.(\d+)$/.exec(jsonPath);
  if (category && Object.hasOwn(baseline.data.taxonomies.product_categories, category[1])) return { jsonPath, selector: `#in-product_cat-${category[1]}`, type: 'checkbox' };
  throw new Error(`Unsupported edit: ${jsonPath}. Media, tab identity/order, tags, and commerce settings are read-only in this editor.`);
}

function validateValue(mapping, value) {
  if (typeof value === 'symbol') throw new Error(`Removed field ${mapping.jsonPath}; use an explicit null to clear text.`);
  if (mapping.type === 'checkbox') {
    if (typeof value !== 'boolean') throw new Error(`${mapping.jsonPath} requires true or false.`);
  } else if (value !== null && typeof value !== 'string') throw new Error(`${mapping.jsonPath} requires a string or null.`);
  if (mapping.jsonPath === 'data.vehicle.name' && (typeof value !== 'string' || !value.trim())) throw new Error('Vehicle name must be a nonempty string.');
}

module.exports = { SITE_URL, COUNTRY_SITES, countrySite, countryForSite, SCHEMA_VERSION, fieldMappings, validateSnapshot, resolveMapping, validateValue };
