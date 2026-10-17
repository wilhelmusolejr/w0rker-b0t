// Button states verified against the loading and saved MHTML references.
async function saveVehicle(page, postId, { timeout = 120_000, log = console.log, siteUrl = 'https://motorhomehire.com', confirmationSelector = '#message.notice-success', confirmationText = /\bPost updated\./i, navigationWaitUntil = 'load', retryClicks = true, buttonSelector = '#publish' } = {}) {
  const deadline = Date.now() + timeout;
  const remaining = () => ({ timeout: Math.max(1, deadline - Date.now()) });
  let stage = 'waiting for the Update button to become enabled';
  try {
    let navigationResult;
    const navigation = page.waitForNavigation({ waitUntil: navigationWaitUntil, ...remaining() }).then(
      () => (navigationResult = { type: 'navigated' }),
      error => (navigationResult = { type: 'error', error }),
    );
    const ready = page.locator(`${buttonSelector}:not([disabled]):not(.disabled):not([aria-disabled="true"])`);
    while (!navigationResult) {
      if (Date.now() >= deadline) throw new Error('Update never became disabled or reloaded the editor.');
      stage = 'waiting for the Update button to become enabled';
      await ready.waitFor({ state: 'visible', ...remaining() });
      // Navigation may finish while waiting for the button. Never click the new page.
      if (navigationResult) break;
      stage = 'clicking Update and waiting for the button to become disabled';
      log(`Vehicle ${postId}: clicking Update.`);
      await ready.click({ noWaitAfter: true, ...remaining() });
      // Legacy classic editors can leave Update enabled until their slow POST
      // returns. One submission and DOMContentLoaded avoid resubmitting a save
      // or clicking the newly reloaded editor while unrelated images still load.
      if (!retryClicks) break;
      const disabled = page.locator(`${buttonSelector}:disabled`).waitFor({
        state: 'visible', timeout: Math.min(5_000, remaining().timeout),
      }).then(
        () => ({ type: 'disabled' }),
        error => error.name === 'TimeoutError' ? { type: 'retry' } : { type: 'error', error },
      );
      // The disabled attribute confirms the click was accepted. A fast reload
      // can win this race before the brief disabled state is observable.
      const outcome = await Promise.race([navigation, disabled]);
      if (outcome.type === 'error') throw outcome.error;
      if (outcome.type !== 'retry') break;
      log(`Vehicle ${postId}: Update is still enabled after 5 seconds; clicking again.`);
    }
    stage = 'waiting for the Update request and page reload';
    log(`Vehicle ${postId}: waiting for the save request and page reload.`);
    const completion = await navigation;
    if (completion.type === 'error') throw completion.error;
    const url = new URL(page.url());
    if (url.origin !== siteUrl || url.pathname !== '/wp-admin/post.php' || url.searchParams.get('post') !== String(postId)) {
      throw new Error('The save returned to an unexpected page. Check the WordPress login and vehicle ID.');
    }
    stage = 'waiting for the WordPress update confirmation';
    log(`Vehicle ${postId}: editor reloaded; waiting for the WordPress update confirmation.`);
    await page.locator(confirmationSelector).filter({ hasText: confirmationText }).first().waitFor({ state: 'visible', ...remaining() });
    stage = 'waiting for the Update button to become ready again';
    await ready.waitFor({ state: 'visible', ...remaining() });
    log(`Vehicle ${postId}: WordPress confirmed the update.`);
  } catch (error) {
    throw new Error(`Vehicle ${postId}: save failed while ${stage}. ${error.message}`, { cause: error });
  }
}

module.exports = { saveVehicle };
