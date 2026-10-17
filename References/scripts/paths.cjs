const path = require('node:path');

const { root, captureFolder } = require('./project.cjs');
module.exports = {
  root,
  currentReference: path.join(captureFolder('motorhomehireProfile'), 'current', '1149 - au - Apollo Endeavour - 4 Berth.json'),
  editorMhtml: path.join(root, 'references', 'motorhomehire', 'Vehicle Edit Profile.mhtml'),
  editorHtml: path.join(root, 'references', 'motorhomehire', 'vehicle-editor.snapshot.html'),
  analysisReport: path.join(root, 'references', 'motorhomehire', 'vehicle-editor-analysis.json'),
};
