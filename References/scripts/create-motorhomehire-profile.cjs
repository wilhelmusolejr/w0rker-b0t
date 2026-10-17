const fs = require('node:fs');
const path = require('node:path');
const { randomUUID } = require('node:crypto');
const store = require('./catalog-store.cjs');
const schema = require('./motorhomehire-create-schema.cjs');
const editor = require('./motorhomehire-create-editor.cjs');
const backend = require('./motorhomehire-search-editor.cjs');
const downloader = require('./download-motorhomehire-search.cjs');
const { importVehicles } = require('./catalog-import.cjs');
const fields = require('./catalog-fields.cjs');
const { loadSettings } = require('./catalog-plan.cjs');
const { root, readSiteConfig } = require('./project.cjs');
const { connectProfile } = require('./hidemium.cjs');

function resolveVehicle(workspace, input) {
  const current=store.scanCurrent(workspace);
  const matches=[...current.values()].filter(({record:r})=>r.identity.country===input.identity.country && schema.normalize(r.identity.brand)===schema.normalize(input.identity.brand) && schema.normalize(r.identity.model)===schema.normalize(input.identity.model) && schema.normalize(r.identity.variant||'')===schema.normalize(input.identity.variant||''));
  if(matches.length>1&&!input.vehicleId)throw new Error('Ambiguous catalog identity. Specify the exact permanent ID.');
  const selected=input.vehicleId?current.get(input.vehicleId):matches[0];
  if(selected) {
    const r=selected.record;
    if(!matches.some(m=>m.record.vehicleId===r.vehicleId))throw new Error('The requested vehicleId has a different canonical identity.');
    if(Object.keys(r.sites.motorhomehireAdmin.records).length)throw new Error('This vehicle already has a linked admin profile. Use the update workflow.');
    return {vehicleId:r.vehicleId,existing:true,identity:store.clone(r.identity)};
  }
  if(input.vehicleId)throw new Error('Explicit vehicleId must exist locally. Use null to resolve or allocate a new permanent ID.');
  const used=new Set(current.keys());
  for(const file of store.jsonFiles(store.inside(workspace,'catalog/import-results')))for(const item of store.readJson(file).imported||[])used.add(item.vehicleId);
  for(const file of store.jsonFiles(store.inside(workspace,'catalog/create-requests')))used.add(store.readJson(file).vehicleId);
  const number=Math.max(0,...[...used].map(id=>Number(id.slice(4))))+1;
  return {vehicleId:'veh_'+String(number).padStart(6,'0'),existing:false,identity:{country:input.identity.country,brand:input.identity.brand,model:input.identity.model,...(input.identity.variant?{variant:input.identity.variant}:{})}};
}
function reportBody(plan) {
  return [`Admin creation run: ${plan.runId}`,`Request: ${plan.input.requestId}`,'Action: create',`Catalog: ${plan.vehicle.vehicleId} (${plan.vehicle.existing?'link existing vehicle':'new local vehicle'})`,`Country: ${plan.vehicle.identity.country} (catalog identity; Add form has no country control)`,`Target: ${schema.SITE}/vehicles/add`,'Save creates a backend record; the Add form has no draft/status control.','',...Object.entries(plan.input.fields).map(([key,value])=>`${key}: ${JSON.stringify(value)}`),`insurance_table: ${JSON.stringify(plan.input.insurance_table)}`,`images: ${JSON.stringify(plan.images)}`,'','The provider ID is fixed. The numeric website ID will be discovered after Save.','Execution rechecks the live list for duplicate name/provider ID within the selected company.','An uncertain submission is recovered from its saved before-list; it is never submitted again.','Review this exact creation and change only the final status line.'].join('\n')+'\n';
}
function createReport(workspace, sourceFile) {
  return store.withLock(workspace,'admin-create-plans',()=>{
    const source=store.inside(workspace,sourceFile), raw=store.readJson(source), input=schema.validate(raw);
    const duplicates=schema.localDuplicates(workspace,input);
    if(duplicates.length)throw new Error('Existing local admin profile(s): '+duplicates.map(d=>`${d.externalId} ${d.name} (${d.source})`).join('; '));
    const ledger=store.inside(workspace,`catalog/create-requests/${input.requestId}.json`);
    if(fs.existsSync(ledger))throw new Error('This requestId is already reserved. Resume its existing run instead of preparing another creation.');
    const vehicle=resolveVehicle(workspace,input), images=schema.imageManifest(workspace,input.images);
    const runId='CREATE-'+new Date().toISOString().replace(/\D/g,'')+'-'+randomUUID().slice(0,8);
    const folder=store.inside(workspace,'changes/'+runId);
    const plan={schemaVersion:'1.0',runId,createdAt:new Date().toISOString(),sourceFile:path.relative(workspace,source),sourceHash:store.hash(raw),input,vehicle,images,config:readSiteConfig('motorhomehireAdmin',workspace),settings:loadSettings(workspace)};
    plan.planHash=store.hash(plan);
    const body=reportBody(plan);
    store.atomicWrite(path.join(folder,'plan.json'),plan);
    store.atomicWrite(path.join(folder,'report.txt'),body+'\nstatus: pending\n');
    store.atomicWrite(ledger,{requestId:input.requestId,runId,vehicleId:vehicle.vehicleId});
    return {plan,folder};
  });
}
function check(workspace, runId, {approved=true}={}) {
  if(!/^CREATE-[a-zA-Z0-9_-]+$/.test(runId||''))throw new Error('Use a generated CREATE run ID.');
  const folder=store.inside(workspace,'changes/'+runId), plan=store.readJson(path.join(folder,'plan.json'));
  const {planHash,...payload}=plan;
  if(plan.runId!==runId || planHash!==store.hash(payload))throw new Error('The frozen creation plan changed.');
  if(!store.equal(schema.validate(plan.input),plan.input))throw new Error('Frozen creation input is invalid.');
  const report=fs.readFileSync(path.join(folder,'report.txt'),'utf8').replace(/^\uFEFF/,'').replace(/\r\n?/g,'\n');
  const footer=/\nstatus: (pending|approved)\s*$/.exec(report);
  if(!footer || report.slice(0,footer.index)!==reportBody(plan))throw new Error('The creation report changed beyond its status line.');
  if(approved && footer[1]!=='approved')throw new Error('Review the creation report and set status: approved first.');
  if(!store.equal(plan.config,readSiteConfig('motorhomehireAdmin',workspace)) || !store.equal(plan.settings,loadSettings(workspace)))throw new Error('Creation settings changed after review.');
  const ledger=store.readJson(store.inside(workspace,`catalog/create-requests/${plan.input.requestId}.json`));
  if(ledger.runId!==runId || ledger.vehicleId!==plan.vehicle.vehicleId)throw new Error('Creation request reservation changed.');
  if(store.hash(store.readJson(store.inside(workspace,plan.sourceFile)))!==plan.sourceHash)throw new Error('The source JSON changed after report preparation.');
  if(!store.equal(schema.imageManifest(workspace,plan.input.images),plan.images))throw new Error('An input image changed after review.');
  return {plan,folder};
}
// The live list may report company ID 0; the company name then identifies the
// company, and a row without one counts as a possible duplicate of any company.
const sameCompany=(row,company)=>row.companyId!=='0'?row.companyId===company.value:!row.company||schema.normalize(row.company)===schema.normalize(company.text);
function matchingRows(list,input) { return list.vehicles.filter(row=>sameCompany(row,input.fields.rental_company) && (schema.normalize(row.name)===schema.normalize(input.fields.name_vehicle)||row.providerId.toUpperCase()===input.fields.vehicle_id_provider)); }
function discover(before,after,input) {
  const old=new Set(before.vehicles.map(row=>row.id));
  const matches=matchingRows(after,input).filter(row=>!old.has(row.id));
  if(matches.length!==1 || matches[0].providerId!==input.fields.vehicle_id_provider || schema.normalize(matches[0].name)!==schema.normalize(input.fields.name_vehicle))throw new Error('Cannot identify exactly one new matching admin profile. Creation needs recovery; no second Save will be attempted.');
  return matches[0].id;
}
function incorporateCreated(workspace,plan,snapshot,folder) {
  const id=snapshot.source.vehicle_id, snapshotFile=path.join(folder,'verified-snapshot.json');
  fields.validateSnapshot('motorhomehireAdmin',snapshot,id);
  store.atomicWrite(snapshotFile,store.sanitize(snapshot));
  const current=store.scanCurrent(workspace), existing=current.get(plan.vehicle.vehicleId);
  for(const entry of current.values())if(entry.record.sites.motorhomehireAdmin.records[id] && entry.record.vehicleId!==plan.vehicle.vehicleId)throw new Error('The created website ID is linked to another catalog vehicle.');
  if(!plan.vehicle.existing) {
    if(existing) {
      if(existing.record.sites.motorhomehireAdmin.records[id] && store.equal(existing.record.identity,plan.vehicle.identity))return existing.file;
      throw new Error('The reserved catalog ID is occupied; the saved website capture remains in this run for recovery.');
    }
    const manifest={schemaVersion:'1.0',vehicles:[{vehicleId:plan.vehicle.vehicleId,identity:plan.vehicle.identity,targets:[{siteId:'motorhomehireAdmin',externalId:id,sourceFile:path.relative(workspace,snapshotFile),role:'primary',confirmed:true}]}]};
    const file=path.join(folder,'import-created.json');store.atomicWrite(file,manifest);
    return importVehicles(workspace,path.relative(workspace,file)).imported[0].file;
  }
  return store.withLock(workspace,plan.vehicle.vehicleId,()=>{
    const entry=store.findCurrent(workspace,plan.vehicle.vehicleId), record=store.clone(entry.record);
    if(!store.equal(record.identity,plan.vehicle.identity))throw new Error('Catalog identity changed during creation.');
    const site=record.sites.motorhomehireAdmin;
    if(site.records[id])return entry.file;
    if(Object.keys(site.records).length)throw new Error('An admin profile was linked to this vehicle during creation.');
    site.availability='linked';
    site.records[id]={mapping:{status:'confirmed',role:'primary',entityType:'backendVehicle',siteOrigin:schema.SITE,editUrl:snapshot.source.url,publicUrl:snapshot.fields.detail_link||null},observed:store.observation(snapshot),lastReadAttempt:{at:new Date().toISOString(),status:'success',phase:'afterCreate'}};
    record.catalogRevision++;
    return store.persist(workspace,fields.reconcile(record,plan.settings.policies),entry.file);
  });
}
const defaults={
  connect:connectProfile,
  list:(page,timeout,log)=>downloader.collectVehicles(page,timeout,log,true),
  fill:(page,plan,workspace)=>editor.fill(page,plan.input,plan.images,{root:workspace,timeout:plan.config.pageTimeoutMs||60000}),
  async capture(page,id,timeout) {await backend.targetEditor(page,id,timeout); return backend.snapshot(await backend.readForm(page),{source:{vehicle_id:id}});},
  verify:editor.verify,
  async submit(page,timeout) {
    const responsePromise=page.waitForResponse(r=>new URL(r.url()).origin===schema.SITE && new URL(r.url()).pathname==='/vehicles/store' && r.request().method()==='POST',{timeout}).catch(()=>null);
    await page.locator('#saveDataVehicle').click({timeout});
    const response=await responsePromise;
    if(!response)throw new Error('No Save response. Recover this creation without submitting again.');
    if(!response.ok())throw new Error(`Admin Save returned HTTP ${response.status()}. Inspect this run before proceeding.`);
    const body=await response.json().catch(()=>null);
    if(body && (body.success===false || body.result===false || ['error','failed'].includes(body.status)))throw new Error('Admin rejected Save.');
    await page.waitForURL(`${schema.SITE}/vehicles`,{waitUntil:'domcontentloaded',timeout});
  },
};
async function execute(workspace,runId,{fillOnly=false,log=console.log,...overrides}={}) {
  const io={...defaults,...overrides};
  return store.withAsyncLock(workspace,'motorhomehire-admin-create',async()=>{
    const {plan,folder}=check(workspace,runId), resultFile=path.join(folder,'results.json');
    let result=fs.existsSync(resultFile)?store.readJson(resultFile):{runId,planHash:plan.planHash,status:'pending',requestId:plan.input.requestId,attempted:false,externalId:null};
    if(result.runId!==runId||result.planHash!==plan.planHash)throw new Error('Creation journal does not match the frozen plan.');
    if(result.status==='done')return result;
    if(fillOnly && result.attempted)throw new Error('This creation was already submitted. Resume it with --run to recover.');
    const persist=()=>{store.atomicWrite(resultFile,result);store.atomicWrite(path.join(folder,'results.txt'),`Run: ${runId}\nStatus: ${result.status}\nProvider ID: ${plan.input.fields.vehicle_id_provider}\nWebsite ID: ${result.externalId||'not yet discovered'}\n${result.error||''}\n`);};
    const lock='browser-'+store.hash({profileId:plan.config.hidemiumProfileId,apiUrl:plan.config.hidemiumApiUrl||'http://127.0.0.1:2222'}).slice(0,24);
    return store.withAsyncLock(workspace,lock,async()=>{
      const timeout=plan.config.pageTimeoutMs||60000;
      let browser,page,keepPage=false;
      try {
        // Recheck local identity/allocation before any website Save.
        if(!result.attempted) {
          const current=store.scanCurrent(workspace).get(plan.vehicle.vehicleId);
          if(plan.vehicle.existing ? !current || !store.equal(current.record.identity,plan.vehicle.identity) || Object.keys(current.record.sites.motorhomehireAdmin.records).length : !!current)throw new Error('The planned catalog identity/link changed.');
          const duplicates=schema.localDuplicates(workspace,plan.input);
          if(duplicates.length)throw new Error('A local duplicate appeared after review.');
        }
        browser=await io.connect({profileId:plan.config.hidemiumProfileId,apiUrl:plan.config.hidemiumApiUrl||'http://127.0.0.1:2222',closeOnConnectFailure:false});
        page=await browser.contexts()[0].newPage();
        page.setDefaultTimeout(timeout);
        if(!result.attempted) {
          const before=await io.list(page,timeout,log);
          if(matchingRows(before,plan.input).length)throw new Error('A matching name or provider ID already exists for this company in the live admin list.');
          store.atomicWrite(path.join(folder,'before-list.json'),before);
          if(io.openAdd)await io.openAdd(page,timeout);else await editor.openAdd(page,timeout);
          const beforeForm=await io.fill(page,plan,workspace);
          store.atomicWrite(path.join(folder,'before-form.json'),store.sanitize(beforeForm));
          if(fillOnly) {result.status='filled';persist();keepPage=true;log('JSON filled and validated. The Add page remains open for review; Save was not clicked.');return result;}
          result.attempted=true;result.status='submitting';result.submittedAt=new Date().toISOString();persist();
          try {await io.submit(page,timeout);}catch(error){result.saveError=error.message;persist();}
        }
        if(!result.externalId) {
          const before=store.readJson(path.join(folder,'before-list.json'));
          const after=await io.list(page,timeout,log);
          store.atomicWrite(path.join(folder,'after-list.json'),after);
          result.externalId=discover(before,after,plan.input);result.status='created';persist();
        }
        // Capture the actual reloaded editor. Persist recovery evidence before verifying or catalog writes.
        const snapshot=await io.capture(page,result.externalId,timeout);
        store.atomicWrite(path.join(folder,'after-snapshot.json'),store.sanitize(snapshot));
        await io.verify(page,plan.input,snapshot);
        result.catalogFile=incorporateCreated(workspace,plan,snapshot,folder);
        result.status='done';result.verifiedAt=new Date().toISOString();delete result.error;persist();
        return result;
      }catch(error){result.status=result.attempted?'needsRecovery':'failedBeforeSave';result.error=error.message;persist();throw error;}
      finally {if(page&&!keepPage&&!page.isClosed())await page.close();if(browser)await browser.close();}
    });
  });
}
async function main(args=process.argv.slice(2),workspace=root) {
  if(args.length===1&&args[0]==='--help') {console.log('Create Motorhome Hire admin profiles from JSON:\n  --report INPUT.json\n  --check CREATE-RUN-ID\n  --fill CREATE-RUN-ID (fill for review; no Save)\n  --run CREATE-RUN-ID (one Save, recovery, verification, catalog incorporation)');return;}
  if(args.length!==2||!['--report','--check','--fill','--run'].includes(args[0]))throw new Error('Use --help for creation commands.');
  if(args[0]==='--report') {const r=createReport(workspace,args[1]);console.log(`Review: ${path.relative(workspace,r.folder)}/report.txt\nRun ID: ${r.plan.runId}`);return r;}
  if(args[0]==='--check') {const r=check(workspace,args[1]);console.log(`Approved creation: ${r.plan.input.fields.name_vehicle}, provider ID ${r.plan.input.fields.vehicle_id_provider}.`);return r;}
  const r=await execute(workspace,args[1],{fillOnly:args[0]==='--fill'});console.log(`${r.status}: changes/${args[1]}/results.txt`);return r;
}
if(require.main===module)main().catch(error=>{console.error(error.message);process.exitCode=1;});
module.exports={resolveVehicle,reportBody,createReport,check,matchingRows,discover,incorporateCreated,execute,main};
