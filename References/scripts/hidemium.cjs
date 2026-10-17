const { chromium } = require('playwright');

async function connectProfile(options = {}) {
  const profileId = options.profileId || process.env.HIDEMIUM_PROFILE_ID || 'local-ae711d21-5ca9-42d4-8c28-4c12cbc534a3';
  const apiUrl = options.apiUrl || process.env.HIDEMIUM_API_URL || 'http://127.0.0.1:2222';
  const url = new URL('/openProfile', apiUrl);
  url.searchParams.set('uuid', profileId);
  const response = await fetch(url, { signal: AbortSignal.timeout(60_000) });
  if (!response.ok) throw new Error(`Hidemium API returned HTTP ${response.status}.`);
  const result = await response.json();
  if (result.status !== 'successfully' || !result.data) {
    throw new Error(`Hidemium could not open the profile: ${result.message || result.status}`);
  }
  const port = Number(result.data.remote_port);
  const endpoint = result.data.web_socket || (Number.isInteger(port) && port > 0 && port < 65536 ? `http://127.0.0.1:${port}` : null);
  if (!endpoint) throw new Error('Hidemium did not return a debugging endpoint.');
  try {
    return await chromium.connectOverCDP(endpoint, { noDefaults: true, timeout: 60_000 });
  } catch (error) {
    // Manual tab sessions must remain open even when CDP attachment fails.
    if (options.closeOnConnectFailure === false) throw error;
    // The API already opened the profile; close it if attachment fails.
    try { await closeProfile({ profileId, apiUrl }); }
    catch (cleanupError) { throw new Error(`${error.message} | Browser cleanup failed: ${cleanupError.message}`); }
    throw error;
  }
}

async function closeProfile(options = {}) {
  const profileId = options.profileId || process.env.HIDEMIUM_PROFILE_ID || 'local-ae711d21-5ca9-42d4-8c28-4c12cbc534a3';
  const apiUrl = options.apiUrl || process.env.HIDEMIUM_API_URL || 'http://127.0.0.1:2222';
  const url = new URL('/closeProfile', apiUrl);
  url.searchParams.set('uuid', profileId);
  const response = await fetch(url, { signal: AbortSignal.timeout(30_000) });
  if (!response.ok) throw new Error(`Hidemium close-profile API returned HTTP ${response.status}.`);
  const result = await response.json();
  // Some Hidemium builds return a message instead of the documented result:true.
  // Closing an already closed profile also satisfies the cleanup requirement.
  const closedMessage = typeof result.message === 'string' && /^profile (?:is )?(?:already )?closed[.!]?$/i.test(result.message.trim());
  if (result.result !== true && !closedMessage) throw new Error(`Hidemium could not close the profile: ${result.message || JSON.stringify(result)}`);
}

async function prepareAutomationTab(browser, pages) {
  const context = browser.contexts()[0];
  if (!context) throw new Error('No Hidemium browser context found.');
  // Create the replacement first so closing old tabs cannot close the last window.
  const page = await context.newPage();
  pages.push(page);
  page.setDefaultTimeout(30_000);
  for (const browserContext of browser.contexts()) {
    for (const other of browserContext.pages()) {
      if (other !== page && !other.isClosed()) await other.close({ runBeforeUnload: false });
    }
  }
  await page.bringToFront();
  return page;
}

async function closeAutomation(browser, pages, config = {}, stopProfile = closeProfile) {
  const errors = [];
  for (const page of pages) {
    try { if (!page.isClosed()) await page.close({ runBeforeUnload: false }); }
    catch (error) { errors.push(error); }
  }
  // browser.close() on a CDP attachment disconnects; stop the profile explicitly.
  try { await stopProfile({ profileId: config.hidemiumProfileId, apiUrl: config.hidemiumApiUrl }); }
  catch (error) { errors.push(error); }
  try { await browser.close(); } catch (error) { errors.push(error); }
  if (errors.length) throw new Error(`Browser cleanup failed: ${errors.map(error => error.message).join(' | ')}`);
}

module.exports = { connectProfile, closeProfile, prepareAutomationTab, closeAutomation };
