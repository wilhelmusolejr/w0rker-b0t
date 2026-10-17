const fs = require('node:fs');
const path = require('node:path');
const store = require('./catalog-store.cjs');

const root = path.resolve(__dirname, '..');
const captureDirectories = {
  motorhomehireProfile: 'captures/motorhomehire-profile',
  motorhomehireAdmin: 'captures/motorhomehire-search',
  campervanfinderAuProfile: 'captures/campervanfinder-au',
  campervanfinderNzProfile: 'captures/campervanfinder-nz',
};
function captureFolder(siteId, workspace = root) {
  if (!Object.hasOwn(captureDirectories, siteId)) throw new Error(`Unknown capture site: ${siteId}.`);
  return store.inside(workspace, captureDirectories[siteId]);
}
function readSiteConfig(siteId, workspace = root) {
  const sites = store.readJson(store.inside(workspace, 'catalog/config/sites.json'));
  const settings = sites.sites?.[siteId];
  if (!settings?.enabled) throw new Error(`Site is disabled or unconfigured: ${siteId}.`);
  const config = store.readJson(store.inside(workspace, settings.configFile));
  if (typeof config.hidemiumProfileId !== 'string' || !config.hidemiumProfileId.trim()) throw new Error(`Configure the Hidemium profile for ${siteId}.`);
  return config;
}
function atomicWriteText(file, value) {
  const temporary = `${file}.${process.pid}.tmp`;
  try { fs.writeFileSync(temporary, value, 'utf8'); fs.renameSync(temporary, file); }
  finally { if (fs.existsSync(temporary)) fs.unlinkSync(temporary); }
}
module.exports = { root, captureDirectories, captureFolder, readSiteConfig, readJson: store.readJson, atomicWriteText };
