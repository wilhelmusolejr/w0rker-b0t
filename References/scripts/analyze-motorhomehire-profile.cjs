const fs = require('node:fs');
const paths = require('./paths.cjs');
const raw = fs.readFileSync(paths.editorMhtml, 'utf8');
const boundary = raw.match(/boundary="([^"]+)"/)[1];
const part = raw.split('--' + boundary).find(p => /Content-Type: text\/html/.test(p));
const body = part.slice(part.search(/\r?\n\r?\n/) + (part.includes('\r\n') ? 4 : 2));
const decoded = body.replace(/=\r?\n/g, '').replace(/=([0-9A-F]{2})/gi, (_, hex) => String.fromCharCode(parseInt(hex, 16)));
const html = Buffer.from(decoded, 'latin1').toString('utf8');
fs.writeFileSync(paths.editorHtml, html);
const tags = html.match(/<(?:input|textarea|select|button|div)\b[^>]*>/gi) || [];
const attrs = tag => Object.fromEntries([...tag.matchAll(/([\w:-]+)="([^"]*)"/g)].map(m => [m[1], m[2]]));
const fields = tags.map(attrs).filter(a => (a.class || '').split(/\s+/).includes('acf-field') && a['data-name']).map(a => ({name:a['data-name'],key:a['data-key'],type:a['data-type']}));
const controls = tags.filter(t => /^<(input|textarea|select|button)\b/i.test(t)).map(attrs).map(a => ({id:a.id,name:a.name,type:a.type}));
const snapshot = JSON.parse(fs.readFileSync(paths.currentReference, 'utf8'));
const report = {targetPostId:9927, sourceJsonPostId:snapshot.capture.postId, sourceJsonName:snapshot.data.vehicle.name, groups:Object.fromEntries(Object.entries(snapshot.data).map(([k,v]) => [k,Object.keys(v)])), acfFields:fields,controls};
report.fieldMappings = [{jsonPath:'data.vehicle.name',selector:'#title',type:'text'}];
for (const [group, values] of Object.entries(snapshot.data)) {
  if (group === 'vehicle' || group === 'taxonomies') continue;
  for (const [name, value] of Object.entries(values)) {
    const htmlName = group === 'featuresIcons' && name === 'sleeps' ? 'sleepsfeature' : name;
    const field = fields.find(f => f.name === htmlName);
    if (field) report.fieldMappings.push({jsonPath:`data.${group}.${name}`,selector:`.acf-field[data-key="${field.key}"]`,htmlName,type:field.type,jsonValueType:value === null ? 'null' : typeof value});
    else report.fieldMappings.push({jsonPath:`data.${group}.${name}`,type:name === 'featured_image' ? 'wordpress-media' : 'unmapped'});
  }
}
report.taxonomyInputNames = [...new Set(controls.map(c=>c.name).filter(n=>n && n.startsWith('tax_input[')))];
report.notes = ['JSON capture.postId is 1149; the test target is explicitly 9927.', 'featuresIcons.sleeps maps to HTML sleepsfeature; vehicleSpecifications.sleeps maps to HTML sleeps.', 'Text fields containing false need a defined empty/N/A conversion before future bulk edits.', 'Image metadata is snapshot information; media edits use attachment IDs and the media UI.', 'Only data.vehicle.name is currently implemented for writing.'];
fs.writeFileSync(paths.analysisReport, JSON.stringify(report, null, 2));
console.log(JSON.stringify({sourceJsonPostId:report.sourceJsonPostId,groups:report.groups,acfFields:fields,mainControls:controls.filter(a=>['title','publish','post_ID','post_type','content'].includes(a.id)||['post_ID','post_type'].includes(a.name))},null,2));
