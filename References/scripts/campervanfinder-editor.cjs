const { countrySite } = require('./campervanfinder-schema.cjs');
const { loadCampervanfinderPage } = require('./campervanfinder-loading.cjs');

async function targetEditor(page, postId, editUrl, { country = 'au', load = loadCampervanfinderPage, ...loadOptions } = {}) {
  const siteUrl = countrySite(country).origin;
  if (new URL(editUrl).origin !== siteUrl) throw new Error('Campervan Finder editor URL does not match the selected country.');
  await load(page, editUrl, { ...loadOptions, country, kind: 'editor', postId });
  const url = new URL(page.url());
  if (url.origin !== siteUrl || url.pathname !== '/wp-admin/post.php' || url.searchParams.get('post') !== String(postId)) throw new Error('Campervan Finder editor did not open. Check the WordPress login.');
  await page.locator('#title').waitFor({ state: 'visible' });
  if (await page.locator('[name="post_ID"]').inputValue() !== String(postId) || await page.locator('[name="post_type"]').inputValue() !== 'product') throw new Error('Unexpected product identity.');
}

// Hidden custom-tab textareas are legitimate form controls. Native setters and
// change events preserve the form/plugin workflow without clicking its nested Save buttons.
function productOperation({ operation, write = false }) {
  const elements = document.querySelectorAll(operation.selector);
  if (elements.length !== 1) throw new Error(`Missing or duplicate control: ${operation.jsonPath}`);
  const el = elements[0];
  if (el.disabled || el.readOnly) throw new Error(`Control is not editable: ${operation.jsonPath}`);
  if (operation.tabIndex !== undefined) {
    const box = el.closest('.woocommerce_product_tab');
    const token = operation.kind === 'custom' ? `product_tab_${operation.tabId}` : `core_tab_${operation.tabId}`;
    if (!box?.classList.contains(token)) throw new Error(`Tab identity changed for ${operation.jsonPath}. Capture the product again.`);
  }
  const editor = window.tinymce?.get?.(el.id);
  if (write) {
    // WordPress Text mode keeps the requested HTML as the saved source.
    if (editor && !editor.isHidden()) {
      if (!window.switchEditors?.go) throw new Error('WordPress Text mode switch is unavailable.');
      window.switchEditors.go(el.id, 'html');
      if (!editor.isHidden()) throw new Error(`Could not switch ${el.id} to Text mode.`);
    }
    const property = operation.type === 'checkbox' ? 'checked' : 'value';
    const proto = el.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
    Object.getOwnPropertyDescriptor(proto, property).set.call(el, operation.type === 'checkbox' ? operation.value : operation.value ?? '');
    el.dispatchEvent(new Event('input', { bubbles: true }));
    if (window.jQuery) window.jQuery(el).trigger('change');
    else el.dispatchEvent(new Event('change', { bubbles: true }));
  }
  if (operation.type === 'checkbox') return el.checked;
  return String(el.value).replace(/\r\n?/g, '\n');
}


module.exports = { targetEditor, productOperation };
