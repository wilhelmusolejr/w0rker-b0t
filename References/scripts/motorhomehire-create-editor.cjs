const { editable } = require('./motorhomehire-search-schema.cjs');
const { SITE } = require('./motorhomehire-create-schema.cjs');
const store = require('./catalog-store.cjs');
// Executes in the authenticated document. Tokens and numeric database IDs are never input fields.
function inspectForm() {
  const form = document.querySelector('#submit_ajax_vehicle');
  if (!form) throw new Error('The admin vehicle form is missing.');
  const fields = {};
  for (const name of ['vehicle_id_provider','name_vehicle','vehicle_type','rental_company','percentage_prepaid','top_insurance','terms_conditions','min_driver_age','max_driver_age','age_vehicle','age_detail','detail_link']) {
    const controls = form.querySelectorAll(`[name="${name}"]`);
    if (controls.length !== 1) throw new Error('Missing/duplicate creation field: ' + name);
    const el = controls[0], editor = window.CKEDITOR?.instances?.[name];
    fields[name] = el.tagName === 'SELECT' ? { value:el.value, text:el.selectedOptions[0]?.textContent.trim() || '' } : editor ? editor.getData() : el.value;
  }
  const table = form.querySelector('#insurance_table');
  if (!table) throw new Error('Insurance table is missing.');
  const insurance_table = Object.fromEntries([['headers','thead'],['rows','tbody']].map(([key,section]) => [key,[...table.querySelectorAll(section+' > tr')].map(row => [...row.cells].map(cell => {
    const texts = cell.querySelectorAll('input[type="text"], textarea');
    const boxes = cell.querySelectorAll('input[type="checkbox"]');
    if (texts.length !== 1 || boxes.length > 1) throw new Error('Unsupported insurance cell shape.');
    return boxes.length ? [boxes[0].checked,texts[0].value] : texts[0].value;
  }))]));
  const systemId=form.querySelector('[name="vehicle_id"]')?.value || '';
  return { fields, insurance_table, files:[...form.querySelectorAll('input[type="file"]')].map(e=>({name:e.name,names:[...e.files].map(f=>f.name)})), previewNames:[...form.querySelectorAll('.upload__img-box .img-bg')].map(e=>e.dataset.file), hasDatabaseId:systemId!=='' && systemId!=='0' };
}
function comparable({ actual, desired }) {
  const html = value => {
    const doc = new DOMParser().parseFromString(value,'text/html');
    const walk = node => node.nodeType === 3 ? (/^\s*$/.test(node.textContent) ? null : node.textContent.replace(/\s+/g,' ')) : node.nodeType === 1 ? [node.tagName.toLowerCase(), [...node.attributes].map(a=>[a.name,a.value]).sort(), [...node.childNodes].map(walk).filter(v=>v!==null)] : null;
    return [...doc.body.childNodes].map(walk).filter(v=>v!==null);
  };
  for (const key of Object.keys(desired.fields)) {
    const a=actual.fields[key], d=desired.fields[key];
    const same=['top_insurance','terms_conditions'].includes(key) ? JSON.stringify(html(a))===JSON.stringify(html(d)) : JSON.stringify(a)===JSON.stringify(d);
    if (!same) throw new Error('The form changed or filtered the requested value: '+key);
  }
  if (JSON.stringify(actual.insurance_table)!==JSON.stringify(desired.insurance_table)) throw new Error('Insurance table values or structure did not match the JSON.');
  return true;
}
async function openAdd(page, timeout) {
  await page.goto(`${SITE}/vehicles/add`,{waitUntil:'domcontentloaded',timeout});
  if (page.url()!==`${SITE}/vehicles/add`) throw new Error('The Add page did not open. Sign in to Motorhome Hire admin.');
  await page.locator('#submit_ajax_vehicle #name_vehicle').waitFor({state:'visible',timeout});
  await page.waitForFunction(()=>['top_insurance','terms_conditions'].every(k=>window.CKEDITOR?.instances?.[k]?.status==='ready') && !!window.jQuery?.fn?.valid, null,{timeout});
}
async function fill(page, input, images, { timeout=60000, root }={}) {
  const before=await page.evaluate(inspectForm);
  if (before.hasDatabaseId || before.fields.vehicle_id_provider || before.fields.name_vehicle || before.files.some(f=>f.names.length) || before.previewNames.length || before.insurance_table.rows.length || !store.equal(before.insurance_table.headers,[['']])) throw new Error('Expected a fresh empty Add form; refusing to reuse an existing or populated profile.');
  await page.evaluate(fields=>{
    const form=document.querySelector('#submit_ajax_vehicle');
    for(const [name,value] of Object.entries(fields)) {
      const el=form.querySelector(`[name="${name}"]`);
      if(!el || el.disabled || el.readOnly)throw new Error('Uneditable creation control: '+name);
      if(el.tagName==='SELECT') {
        const option=[...el.options].find(o=>o.value===value.value);
        if(!option || option.disabled || option.textContent.trim()!==value.text)throw new Error('Dropdown ID/label mismatch: '+name);
      }
      if(typeof value==='string' && el.maxLength>=0 && value.length>el.maxLength)throw new Error('Field exceeds live length limit: '+name);
      if(['top_insurance','terms_conditions'].includes(name) && window.CKEDITOR?.instances?.[name]?.status!=='ready')throw new Error('Rich text editor is not ready: '+name);
    }
  },input.fields);
  await page.evaluate(async fields=>{
    const form=document.querySelector('#submit_ajax_vehicle');
    for(const [name,value] of Object.entries(fields)) {
      const el=form.querySelector(`[name="${name}"]`), editor=window.CKEDITOR?.instances?.[name];
      if(editor)await new Promise(resolve=>editor.setData(value, {callback:()=>{editor.updateElement(); resolve();}}));
      else {el.value=el.tagName==='SELECT'?value.value:value; el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));}
    }
  },input.fields);
  const {headers,rows}=input.insurance_table;
  for(let column=1;column<headers[0].length;column++)await page.locator('#add_column').click();
  for(const row of rows)await page.locator(Array.isArray(row[1])?'#add_row_checkbox':'#add_row_text').click();
  await page.evaluate(table=>{
    const el=document.querySelector('#insurance_table');
    for(const [key,section] of [['headers','thead'],['rows','tbody']]) {
      const actual=[...el.querySelectorAll(section+' > tr')];
      if(actual.length!==table[key].length)throw new Error('The live insurance controls did not create the expected rows.');
      actual.forEach((row,i)=>{
        if(row.cells.length!==table[key][i].length)throw new Error('The live insurance controls did not create the expected columns.');
        [...row.cells].forEach((cell,j)=>{
          const value=table[key][i][j], text=cell.querySelector('input[type="text"]'), box=cell.querySelector('input[type="checkbox"]');
          if(!text || !!box!==Array.isArray(value))throw new Error('Insurance control shape mismatch.');
          if(box)box.checked=value[0];
          text.value=box?value[1]:value;
          text.dispatchEvent(new Event('change',{bubbles:true}));
        });
      });
    }
  },input.insurance_table);
  for(const image of images.filter(i=>i.role==='featured')) {
    await page.locator('#banner_url_vehicle').setInputFiles(store.inside(root,image.path));
    await page.waitForFunction(()=>document.querySelector('.circle-image .img_change')?.src.startsWith('data:image/'),null,{timeout});
  }
  // The page's FileReaders append previews asynchronously. Upload sequentially to preserve JSON order.
  for(const [index,image] of images.filter(i=>i.role==='gallery').entries()) {
    await page.locator('[name="images[]"]').setInputFiles(store.inside(root,image.path));
    await page.waitForFunction(count=>document.querySelectorAll('#submit_ajax_vehicle .upload__img-box').length===count,index+1,{timeout});
  }
  const filled=await page.evaluate(inspectForm);
  await page.evaluate(comparable,{actual:filled,desired:input});
  const path=require('node:path');
  if(!store.equal(filled.previewNames,images.filter(i=>i.role==='gallery').map(i=>path.basename(i.path))))throw new Error('Gallery previews changed the requested file order.');
  if(input.images.featured && !store.equal(filled.files.find(f=>f.name==='banner_url_vehicle')?.names,[path.basename(input.images.featured)]))throw new Error('Featured image was rejected by the live form.');
  const validation=await page.evaluate(()=>{
    const f=document.querySelector('#submit_ajax_vehicle');
    if(!window.jQuery(f).valid())return [...f.querySelectorAll('label.error')].map(e=>e.textContent.trim()).filter(Boolean).join('; ')||'Live Add form validation failed.';
    return f.checkValidity()?'':'HTML input constraints rejected the creation values.';
  });
  if(validation)throw new Error(validation);
  return before;
}
async function verify(page, input, snapshot) {
  await page.evaluate(comparable,{actual:{fields:snapshot.fields,insurance_table:snapshot.insurance_table},desired:input});
  if(snapshot.images.gallery.length!==input.images.gallery.length)throw new Error('Saved gallery count differs from the reviewed input.');
  if(input.images.featured && !snapshot.images.featured)throw new Error('Featured image was not saved.');
}
module.exports = { inspectForm, comparable, openAdd, fill, verify };
