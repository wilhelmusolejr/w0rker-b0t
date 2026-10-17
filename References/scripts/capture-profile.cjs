const path = require('node:path');
const fields = require('./catalog-fields.cjs');
const adapters = require('./catalog-adapters.cjs');
const project = require('./project.cjs');
const store = require('./catalog-store.cjs');
const { connectProfile, prepareAutomationTab, closeAutomation } = require('./hidemium.cjs');
const { storeCurrentSnapshot } = require('./motorhomehire-profile-capture.cjs');
const { writeCurrent } = require('./campervanfinder-profile.cjs');
const { profileFilename } = require('./download-motorhomehire-search.cjs');

// Replaces the site's captures/<site>/current copy with a fresh snapshot.
function storeCapture(siteId, snapshot, { externalId, country, root } = {}) {
  const directory = project.captureFolder(siteId, root);
  let saved;
  if (siteId === 'motorhomehireProfile') saved = storeCurrentSnapshot(snapshot, { postId: Number(externalId), country }, directory);
  else if (fields.isCampervanFinder(siteId)) saved = writeCurrent(snapshot, directory);
  else {
    saved = { filename: profileFilename(snapshot) };
    store.atomicWrite(path.join(directory, 'current', saved.filename), store.sanitize(snapshot));
  }
  return { ...saved, file: path.relative(root || project.root, path.join(directory, 'current', saved.filename)) };
}
function parseArgs(args) {
  const options = {};
  for (let i = 0; i < args.length; i++) {
    const argument = args[i];
    if (argument === '--help') options.help = true;
    else if (['--site', '--id', '--country'].includes(argument)) {
      const key = argument.slice(2);
      if (options[key] || !args[i + 1] || args[i + 1].startsWith('--')) throw new Error(`Missing or duplicate value for ${argument}.`);
      options[key] = args[++i];
    } else throw new Error(`Unknown option: ${argument}.`);
  }
  if (options.help) return options;
  if (!Object.hasOwn(fields.SITES, options.site || '') || !/^[1-9]\d*$/.test(options.id || '') || !Number.isSafeInteger(Number(options.id))) throw new Error('Use --site SITE_ID --id WEBSITE_VEHICLE_ID.');
  if (options.country && (!['au', 'nz'].includes(options.country) || fields.SITES[options.site].country && fields.SITES[options.site].country !== options.country)) throw new Error('--country must match the selected site.');
  return options;
}
async function main(args = process.argv.slice(2)) {
  const options = parseArgs(args);
  if (options.help) {
    console.log('Read one website profile without saving the website:\n  node scripts/capture-profile.cjs --site SITE_ID --id WEBSITE_ID [--country au|nz]\nSites: ' + Object.keys(fields.SITES).join(', ') + '\nSnapshots go to captures/<site>/current. Use refresh-vehicles.cjs to download saved vehicles and refresh their combined records.');
    return;
  }
  const config = project.readSiteConfig(options.site);
  const site = fields.SITES[options.site];
  const editUrl = options.site === 'motorhomehireAdmin' ? `${site.origin}/vehicles/detail/${options.id}` : `${site.origin}/wp-admin/post.php?post=${options.id}&action=edit${options.site === 'motorhomehireProfile' ? '&lang=en' : ''}`;
  const target = { siteId: options.site, externalId: options.id, baseline: { capture: { editUrl }, source: { vehicle_id: options.id } } };
  const browser = await connectProfile({ profileId: config.hidemiumProfileId, apiUrl: config.hidemiumApiUrl });
  const pages = [];
  let failure;
  try {
    const page = await prepareAutomationTab(browser, pages);
    const snapshot = await adapters.capture(page, target, { timeout: config.pageTimeoutMs || 300000 });
    const saved = storeCapture(options.site, snapshot, { externalId: options.id, country: options.country });
    console.log(`Captured ${saved.file}`);
    return saved;
  } catch (error) { failure = error; throw error; }
  finally {
    try { await closeAutomation(browser, pages, config); }
    catch (error) { throw new Error(`${failure ? failure.message + ' | ' : ''}${error.message}`); }
  }
}
if (require.main === module) main().catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { parseArgs, main, storeCapture };
