const { spawn } = require('node:child_process');
const { root } = require('./paths.cjs');
const store = require('./catalog-store.cjs');
const { checkPlan } = require('./catalog-plan.cjs');
const { main: catalogMain } = require('./catalog-update.cjs');

// A batch launcher must never run another job merely because it is selected.
async function main(args = process.argv.slice(2), workspace = root) {
  const [mode, jobFile, ...options] = args;
  if (!['report', 'check', 'run'].includes(mode) || !jobFile ||
      options.some(option => option !== '--open') || options.length > 1 || options.length && mode !== 'report')
    throw new Error('Usage: catalog-batch.cjs report|check|run JOB_FILE [--open for report]');
  const jobPath = store.inside(workspace, jobFile);
  if (mode === 'report') {
    const result = await catalogMain(['--report', '--job', jobFile], workspace);
    if (options.includes('--open')) {
      const editor = spawn('notepad.exe', [result.reportFile], { detached: true, stdio: 'ignore' });
      editor.on('error', error => console.error(`Report created; open it manually at ${result.reportFile}. ${error.message}`));
      editor.unref();
    }
    return result;
  }
  const runId = store.readJson(store.inside(workspace, 'catalog/config/selection.json')).runId;
  if (!runId) throw new Error('Generate the report for this batch first.');
  const { plan } = checkPlan(workspace, runId, { approved: false });
  if (store.inside(workspace, plan.jobFile) !== jobPath)
    throw new Error(`The selected report belongs to another job (${plan.job.jobId}). Generate this batch's report first.`);
  return catalogMain([`--${mode}`, '--run-id', runId], workspace);
}

if (require.main === module) main().catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { main };
