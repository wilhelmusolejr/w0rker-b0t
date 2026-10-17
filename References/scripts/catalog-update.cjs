const path = require('node:path');
const { root } = require('./paths.cjs');
const { createPlan, checkPlan } = require('./catalog-plan.cjs');
const { importVehicles } = require('./catalog-import.cjs');
const { run } = require('./catalog-runner.cjs');
const store = require('./catalog-store.cjs');

function parseArgs(args) {
  const options = {};
  for (let i = 0; i < args.length; i++) {
    const argument = args[i];
    if (['--report','--check','--run','--help'].includes(argument)) options[argument.slice(2)] = true;
    else if (['--job','--run-id','--import'].includes(argument)) {
      if (!args[i + 1] || args[i + 1].startsWith('--')) throw new Error(`Missing value for ${argument}.`);
      if (options[argument.slice(2)]) throw new Error(`Duplicate option: ${argument}.`);
      options[argument.slice(2)] = args[++i];
    } else throw new Error(`Unknown option: ${argument}.`);
  }
  if (options.help) return options;
  if (['report','check','run','import'].filter(mode => options[mode]).length !== 1) throw new Error('Choose one of --report, --check, --run or --import PATH.');
  if (options.job && !options.report || options['run-id'] && !(options.check || options.run)) throw new Error('--job belongs to --report; --run-id belongs to --check/--run.');
  return options;
}
async function main(args = process.argv.slice(2), workspace = root) {
  const options = parseArgs(args);
  if (options.help) {
    console.log('Combined vehicle updates:\n  --import catalog/import-vehicles.json\n  --report [--job jobs/update-vehicles.json]\n  --check [--run-id RUN-...]\n  --run [--run-id RUN-...]\nCheck/run use catalog/config/selection.json when no RUN ID is passed. Review changes/RUN-.../report.txt and set status: approved before running.');
    return;
  }
  if (options.import) {
    const result = importVehicles(workspace, options.import);
    for (const vehicle of result.imported) console.log(`Imported ${vehicle.vehicleId}: ${vehicle.file} (mappings: ${vehicle.mappingStatus}).`);
    return result;
  }
  if (options.report) {
    const result = createPlan(workspace, options.job || 'jobs/update-vehicles.json');
    console.log(`Pending report: ${path.relative(workspace, result.reportFile)}\nRun ID: ${result.plan.runId}\nReview the report and change its final line to status: approved.`);
    return result;
  }
  const runId = options['run-id'] || store.readJson(store.inside(workspace,'catalog/config/selection.json')).runId;
  if (!runId) throw new Error('Generate a report first, or pass --run-id RUN-... to resume a frozen run.');
  if (options.check) {
    const { plan } = checkPlan(workspace, runId);
    const targets = plan.derived.tasks.flatMap(task => task.vehicles.flatMap(vehicle => vehicle.targets));
    console.log(`Approved ${plan.runId}: ${plan.inputs.length} vehicles, ${targets.length} targets, ${targets.reduce((n, target) => n + target.operations.length, 0)} field instructions, ${targets.filter(target => target.gallery).length} gallery instructions. Check only.`);
    return plan;
  }
  const result = await run(workspace, runId);
  console.log(`${result.status}: changes/${result.runId}/results.txt (field coverage: ${result.coverage}).`);
  if (result.status !== 'done') throw new Error(result.reason);
  return result;
}

if (require.main === module) main().catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { parseArgs, main };
