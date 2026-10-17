const { countrySite } = require('./campervanfinder-schema.cjs');

const DEFAULT_PAGE_TIMEOUT_MS = 300_000;

// Thumbnail ID inputs are intentionally optional: the legacy featured-image
// box uses media-upload.php and may never render one, even on a fully loaded page.
function inspectPageReadiness({ kind }) {
  const selectors = kind === 'editor'
    ? ['#title', '[name="post_ID"]', '[name="post_type"]', '#content', '#excerpt', '#postimagediv', '#woocommerce-product-data', '#woocommerce-product-images', '#product_catchecklist', '#_override_tab_layout', '#tax-input-product_tag']
    : ['.wp-list-table #the-list', '.tablenav.top .displaying-num'];
  const missing = selectors.filter(selector => !document.querySelector(selector));
  if (kind === 'editor' && document.querySelector('#title') && !document.querySelector('#title').value.trim()) missing.push('populated vehicle title');
  const busy = [...document.querySelectorAll('.spinner.is-active, .spinner.active, .blockUI.blockOverlay, #post[aria-busy="true"], .wp-list-table[aria-busy="true"], #publish.disabled, #publish[disabled], #publish[aria-disabled="true"], #save-post.disabled, #save-post[disabled]')].filter(el => {
    const style = getComputedStyle(el);
    return style.display !== 'none' && style.visibility !== 'hidden' && style.opacity !== '0' && el.getClientRects().length;
  }).map(el => el.id || el.className);
  const controls = kind === 'editor'
    ? '#title, #content, #excerpt, .woocommerce_product_tab input, .woocommerce_product_tab textarea, #product_catchecklist input, #woocommerce-product-images li.image, #postimagediv img'
    : '.wp-list-table #the-list, .tablenav.top';
  const signature = JSON.stringify([...document.querySelectorAll(controls)].map(el => [el.id, el.name, el.value ?? el.getAttribute('data-attachment_id') ?? el.getAttribute('src') ?? el.textContent, el.checked]));
  return { readyState: document.readyState, missing, busy, signature, postId: document.querySelector('[name="post_ID"]')?.value, postType: document.querySelector('[name="post_type"]')?.value };
}

async function waitForPageControls(page, { kind, postId, timeout = DEFAULT_PAGE_TIMEOUT_MS, log = console.log, pollMs = 250, settleMs = 1_000, resourceGraceMs = 15_000, heartbeatMs = 15_000 } = {}) {
  const start = Date.now(), deadline = start + timeout;
  let stableSince = start, signature, lastLog = start, state;
  while (Date.now() < deadline) {
    if (page.isClosed()) throw new Error('Automation tab closed while waiting for the page to finish loading.');
    try {
      state = await page.evaluate(inspectPageReadiness, { kind });
    } catch (error) {
      if (!/Execution context was destroyed|Cannot find context|because of a navigation/i.test(error.message)) throw error;
      // A navigation can replace the document while the readiness check runs.
      state = { readyState: 'loading', missing: ['new document'], busy: [], signature: null };
    }
    if (kind === 'editor' && ((state.postId && state.postId !== String(postId)) || (state.postType && state.postType !== 'product'))) throw new Error('Unexpected product identity while waiting for the editor.');
    if (state.signature !== signature || state.missing.length || state.busy.length) {
      stableSince = Date.now();
      signature = state.signature;
    }
    const elapsed = Date.now() - start;
    const controlsReady = !state.missing.length && !state.busy.length && state.readyState !== 'loading' && Date.now() - stableSince >= settleMs;
    if (controlsReady && (state.readyState === 'complete' || elapsed >= resourceGraceMs)) {
      if (state.readyState !== 'complete') log('Background resources are still loading; required page controls are complete and stable. Continuing.');
      return state;
    }
    if (Date.now() - lastLog >= heartbeatMs) {
      const detail = state.missing.length ? `waiting for ${state.missing.join(', ')}` : state.busy.length ? `busy: ${state.busy.join(', ')}` : 'waiting for document resources/controls to settle';
      log(`Still loading ${kind} (${Math.round(elapsed / 1_000)}s): document=${state.readyState}; ${detail}.`);
      lastLog = Date.now();
    }
    await page.waitForTimeout(Math.min(pollMs, Math.max(1, deadline - Date.now())));
  }
  const detail = state?.missing.length ? `Missing: ${state.missing.join(', ')}.` : state?.busy.length ? `Busy: ${state.busy.join(', ')}.` : `Document state: ${state?.readyState || 'unknown'}.`;
  throw new Error(`Timed out waiting for ${kind} controls after ${Math.round(timeout / 1_000)}s. ${detail}`);
}

async function loadCampervanfinderPage(page, url, options = {}) {
  const { kind = 'editor', postId, timeout = DEFAULT_PAGE_TIMEOUT_MS, log = console.log, heartbeatMs = 15_000 } = options;
  const siteUrl = countrySite(options.country || 'au').origin;
  if (new URL(url).origin !== siteUrl) throw new Error('Campervan Finder URL does not match the selected country.');
  if (!Number.isSafeInteger(timeout) || timeout <= 0) throw new Error('pageTimeoutMs must be a positive integer.');
  const start = Date.now();
  const timer = setInterval(() => log(`Still loading ${kind}: waiting for the document response (${Math.round((Date.now() - start) / 1_000)}s, limit ${Math.round(timeout / 1_000)}s).`), heartbeatMs);
  let response;
  try {
    // External images, embeds, and tracking scripts can hold the load event open
    // indefinitely. Wait for the document, then inspect the actual editor/list.
    response = await page.goto(url, { waitUntil: 'domcontentloaded', timeout });
  } catch (error) {
    throw new Error(`Could not load ${kind} document within ${Math.round(timeout / 1_000)}s: ${error.message}`, { cause: error });
  } finally { clearInterval(timer); }
  if (response && response.status() >= 400) throw new Error(`The ${kind} page returned HTTP ${response.status()}.`);
  const actual = new URL(page.url()), expected = new URL(url);
  if (actual.origin !== siteUrl || actual.pathname !== expected.pathname || (kind === 'editor' && actual.searchParams.get('post') !== String(postId)) || (kind === 'list' && actual.searchParams.get('post_type') !== 'product')) throw new Error(`The expected ${kind} page did not open. Check the WordPress login and URL.`);
  const remaining = Math.max(1, timeout - (Date.now() - start));
  return waitForPageControls(page, { ...options, kind, postId, log, timeout: remaining });
}

module.exports = { DEFAULT_PAGE_TIMEOUT_MS, inspectPageReadiness, waitForPageControls, loadCampervanfinderPage };
