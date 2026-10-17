// Motorhome Hire editor controls. Execution and approval belong to catalog-runner.
function formText(value) {
  return value === null || value === false ? '' : String(value);
}

async function targetEditor(page, vehicle) {
  await page.goto(vehicle.editUrl, { waitUntil: 'load', timeout: 60_000 });
  const actualUrl = new URL(page.url());
  if (actualUrl.origin !== 'https://motorhomehire.com' || actualUrl.pathname !== '/wp-admin/post.php' || actualUrl.searchParams.get('post') !== String(vehicle.postId)) throw new Error(`Vehicle ${vehicle.postId}: editor did not open. Check the WordPress login in Hidemium.`);
  await page.locator('#title').waitFor({ state: 'visible' });
  if (await page.locator('[name="post_ID"]').inputValue() !== String(vehicle.postId) || await page.locator('[name="post_type"]').inputValue() !== 'vehicle') throw new Error(`Vehicle ${vehicle.postId}: unexpected post identity. Refusing to edit.`);
}

async function fieldControl(page, operation) {
  if (operation.selector === '#title') return page.locator('#title');
  const field = page.locator(operation.selector);
  if (await field.count() !== 1) throw new Error(`Expected one editor field for ${operation.jsonPath}.`);
  const box = field.locator('xpath=ancestor::*[contains(concat(" ", normalize-space(@class), " "), " postbox ")][1]');
  if (await box.count() && await box.evaluate(el => el.classList.contains('closed'))) await box.locator('.hndle').click();
  return field;
}

async function checkboxChoices(field, operation) {
  const inputs = field.locator('input[type="checkbox"]');
  const labels = await inputs.evaluateAll(elements => elements.map(el => el.closest('label')?.textContent.trim().toLowerCase()));
  if (labels.length === 1) return { inputs, single: true };
  const yes = labels.indexOf('yes');
  const no = labels.indexOf('no');
  if (labels.length !== 2 || yes < 0 || no < 0) throw new Error(`Cannot identify Yes/No choices for ${operation.jsonPath}.`);
  return { inputs, yes, no };
}

async function resolveSelect(field, operation) {
  const select = field.locator('select');
  const options = await select.locator('option').evaluateAll(elements => elements.map(el => ({ value: el.value, label: el.textContent.trim() })));
  const desired = formText(operation.value);
  const exact = options.find(option => option.value === desired) || options.find(option => option.label === desired);
  if (!exact) throw new Error(`No select option for ${operation.jsonPath}: ${JSON.stringify(desired)}`);
  return { select, desired: exact.value };
}

async function readOperation(page, operation) {
  const field = await fieldControl(page, operation);
  if (operation.type === 'checkbox') {
    const choices = await checkboxChoices(field, operation);
    if (choices.single) return choices.inputs.first().isChecked();
    const yes = await choices.inputs.nth(choices.yes).isChecked();
    const no = await choices.inputs.nth(choices.no).isChecked();
    return yes !== no ? yes : null;
  }
  if (operation.type === 'select') {
    const { select } = await resolveSelect(field, operation);
    return select.inputValue();
  }
  const input = operation.selector === '#title' ? field : field.locator(operation.type === 'textarea' ? 'textarea' : 'input[type="text"]');
  return input.inputValue();
}

async function desiredValue(page, operation) {
  if (operation.type === 'checkbox') return operation.value;
  if (operation.type === 'select') return (await resolveSelect(await fieldControl(page, operation), operation)).desired;
  return formText(operation.value).replace(/\r\n/g, '\n');
}

async function writeOperation(page, operation) {
  const field = await fieldControl(page, operation);
  if (operation.type === 'checkbox') {
    const choices = await checkboxChoices(field, operation);
    if (choices.single) await choices.inputs.first().setChecked(operation.value);
    else {
      await choices.inputs.nth(operation.value ? choices.no : choices.yes).setChecked(false);
      await choices.inputs.nth(operation.value ? choices.yes : choices.no).setChecked(true);
    }
  } else if (operation.type === 'select') {
    const { select, desired } = await resolveSelect(field, operation);
    await select.selectOption(desired);
  } else {
    const input = operation.selector === '#title' ? field : field.locator(operation.type === 'textarea' ? 'textarea' : 'input[type="text"]');
    await input.fill(formText(operation.value));
  }
}


module.exports = { formText, targetEditor, readOperation, writeOperation, desiredValue };
