// Motorhome Hire public vehicle (WordPress) draft shape for new profiles.
// Options, required flags and taxonomy terms verified against the saved
// references/motorhomehire/Add New Vehicle page (post-new.php?post_type=vehicle).
const { get } = require('./catalog-store.cjs');
const mh = require('./motorhomehire-profile-schema.cjs');
const SC = require('../Source comparison/source-compare.js');

const DRAFT_TYPE = 'motorhomehireFrontendDraft';
const SCHEMA = '1.0';
const formFields = mh.fieldMappings.filter(f => ['text','textarea','select','checkbox'].includes(f.type));
// The Add page marks every ACF field required except these (and the gallery).
const optional = new Set(['data.descriptionAndImages.description','data.descriptionAndImages.youtube','data.descriptionAndImages.3d_view','data.featuresIcons.toilets','data.featuresIcons.showers']);
// Values the empty Add page already holds; a field left unknown keeps them.
const addDefaults = {
  'data.importantInformation.minimum_driver_age': '21',
  'data.importantInformation.driving_license': 'Domestic<br>\nInternational (must be officially translated)',
  'data.standardInclusions.kilometres_included': 'Unlimited',
  'data.vehicleSpecifications.dimensions': 'Length:\nWidth:\nHeight:\nInterior Height:',
};
const selectOptions = {
  'data.vehicleSpecifications.transmission': ['Automatic','Manual','Automatic/Manual'],
  'data.vehicleSpecifications.seatbelts': ['2 in Drivers Cabin','2 in Drivers Cabin + 1 in Rear','2 in Drivers Cabin + 2 in Rear','2 in Drivers Cabin + 3 in Rear','2 in Drivers Cabin + 4 in Rear','3 in Drivers Cabin','3 in Drivers Cabin + 2 in Rear'],
  'data.vehicleSpecifications.fuel_type': ['Petrol','Diesel','Petrol or Diesel (cannot be requested)'],
};
const brandTerms = ['Apollo','Autorent','Autosleepers','Awesome','Britz','Calypso Campers','Camperman','Campervan Village','Cheapa','Cruisin','Elements RV','Freedom','GoCheap','Happy Campers','Hippie','Jucy','LeisureRent',"Let's Go",'Lucky Rental','Maui','Mighty','Spaceships','Star RV','Tasmania Campers','Travellers Autobarn','Tui Campers'];
// [name, parent name or null]. Country roots are Australia, New Zealand and USA.
const locationTerms = [
  ['Australia',null],['Australian Capital Territory','Australia'],['Canberra','Australian Capital Territory'],
  ['New South Wales','Australia'],['Coffs Harbour','New South Wales'],['Sydney','New South Wales'],
  ['Northern Territory','Australia'],['Alice Springs','Northern Territory'],['Darwin','Northern Territory'],
  ['Queensland','Australia'],['Airlie Beach','Queensland'],['Bamaga','Queensland'],['Brisbane','Queensland'],['Cairns','Queensland'],['Gold Coast','Queensland'],['Townsville','Queensland'],
  ['South Australia','Australia'],['Adelaide','South Australia'],
  ['Tasmania','Australia'],['Devonport','Tasmania'],['Hobart','Tasmania'],['Launceston','Tasmania'],
  ['Victoria','Australia'],['Geelong','Victoria'],['Gippsland','Victoria'],['Melbourne','Victoria'],
  ['Western Australia','Australia'],['Broome','Western Australia'],['Exmouth','Western Australia'],['Perth','Western Australia'],
  ['New Zealand',null],['Auckland','New Zealand'],['Blenheim','New Zealand'],['Christchurch','New Zealand'],['Dunedin','New Zealand'],['Greymouth','New Zealand'],['Hamilton','New Zealand'],['Nelson','New Zealand'],['Picton','New Zealand'],['Queenstown','New Zealand'],['Wellington','New Zealand'],
  ['USA',null],
];
const locationParent = new Map(locationTerms);
const countryRoot = { au: 'Australia', nz: 'New Zealand', us: 'USA' };
const specialFeatures = Object.keys(mh.specialFeatureLabels);
const sourceTypes = {
  catalogSite: ['site','externalId'], sharedFact: ['fact'], brandSiblings: ['postIds'], official: ['url'],
  generated: ['generator'], supplied: ['note'], derived: ['note'], aggregator: ['url'], other: ['url'],
};
// The website cannot leave these empty: selects default to their first option
// and a profile without a title, brand or location is unusable.
const blocking = ['data.vehicle.name','data.vehicleSpecifications.transmission','data.vehicleSpecifications.seatbelts','data.vehicleSpecifications.fuel_type','data.taxonomies.brand','data.taxonomies.rental_locations'];
const normalize = value => String(value).normalize('NFKD').replace(/[̀-ͯ]/g, '').toLowerCase().replace(/[^a-z0-9]/g, '');
const pointer = jsonPath => '/' + jsonPath.replace(/\./g, '/');
const taxonomyPaths = ['data.taxonomies.brand','data.taxonomies.rental_locations',...specialFeatures.map(key => `data.taxonomies.special_features.${key}`)];
const allPaths = [...formFields.map(f => f.jsonPath), ...taxonomyPaths];

function blankData() {
  const data = { taxonomies: { brand: null, rental_locations: null, special_features: Object.fromEntries(specialFeatures.map(key => [key, null])) } };
  for (const field of formFields) {
    const [, group, key] = field.jsonPath.split('.');
    (data[group] ||= {})[key] = null;
  }
  return data;
}
function shapeErrors(data, template = blankData(), at = 'data') {
  if (!data || typeof data !== 'object' || Array.isArray(data)) return [`${at} must be an object.`];
  const errors = [];
  for (const key of Object.keys(data)) if (!Object.hasOwn(template, key)) errors.push(`Unknown property: ${at}.${key}`);
  for (const [key, value] of Object.entries(template)) {
    if (!Object.hasOwn(data, key)) errors.push(`Missing property: ${at}.${key}`);
    else if (value && typeof value === 'object') errors.push(...shapeErrors(data[key], value, `${at}.${key}`));
  }
  return errors;
}
function valueErrors(data, country) {
  const errors = [];
  for (const field of formFields) {
    const value = get({ data }, pointer(field.jsonPath));
    if (value === null || value === undefined) continue;
    if (field.type === 'checkbox' && typeof value !== 'boolean') errors.push(`${field.jsonPath} must be true, false or null.`);
    else if (field.type !== 'checkbox' && typeof value !== 'string') errors.push(`${field.jsonPath} must be text or null.`);
    else if (field.type === 'select' && !selectOptions[field.jsonPath].includes(value)) errors.push(`${field.jsonPath} must be one of: ${selectOptions[field.jsonPath].join(' | ')}.`);
    else if (typeof value === 'string' && value.includes('\0')) errors.push(`${field.jsonPath} contains a null character.`);
  }
  const name = data.vehicle?.name;
  if (typeof name === 'string' && (!name.trim() || name.length > 200)) errors.push('data.vehicle.name must be nonempty text up to 200 characters.');
  const age = data.importantInformation?.minimum_driver_age;
  if (typeof age === 'string' && !/^\d{2}$/.test(age)) errors.push('data.importantInformation.minimum_driver_age must be a whole number such as "21".');
  const tax = data.taxonomies || {};
  if (tax.brand !== null && !brandTerms.includes(tax.brand)) errors.push(`data.taxonomies.brand must be an existing brand term: ${brandTerms.join(', ')}.`);
  if (tax.rental_locations !== null) {
    if (!Array.isArray(tax.rental_locations) || !tax.rental_locations.length) errors.push('data.taxonomies.rental_locations must be a nonempty array of location terms or null.');
    else {
      const seen = new Set();
      for (const location of tax.rental_locations) {
        if (!locationParent.has(location)) { errors.push(`Unknown rental location term: ${location}`); continue; }
        if (seen.has(location)) errors.push(`Duplicate rental location: ${location}`);
        seen.add(location);
        let root = location;
        while (locationParent.get(root)) root = locationParent.get(root);
        if (root !== countryRoot[country]) errors.push(`Rental location ${location} is outside ${countryRoot[country] || country}.`);
      }
    }
  }
  for (const key of specialFeatures) {
    const value = tax.special_features?.[key];
    if (value !== null && value !== undefined && typeof value !== 'boolean') errors.push(`data.taxonomies.special_features.${key} must be true, false or null.`);
  }
  // Shared format rules ("Source comparison/AGENTS.md"); icon texts are stored as site HTML here.
  for (const field of SC.FIELDS.filter(f => !['featuresIcons.toilets','featuresIcons.showers'].includes(f.path) && ['text','textarea'].includes(f.type))) {
    const value = get({ data }, pointer('data.' + field.path));
    if (typeof value === 'string') for (const issue of SC.formatIssues(field.path, value)) if (issue.level === 'error') errors.push(`data.${field.path}: ${issue.message}.`);
  }
  for (const [key, label] of [['toilets','toilet'],['showers','shower']]) {
    const value = data.featuresIcons?.[key], name = label[0].toUpperCase() + label.slice(1);
    if (typeof value === 'string' && value && value !== name && value !== `<span class="no${label}">${name}</span>`) errors.push(`data.featuresIcons.${key} must be "${name}", <span class="no${label}">${name}</span>, "" or null.`);
  }
  return errors;
}
function sourceErrors(draft) {
  const errors = [], sources = draft.sources;
  if (!sources || typeof sources !== 'object' || Array.isArray(sources)) return ['sources must be an object keyed by field path.'];
  for (const [jsonPath, list] of Object.entries(sources)) {
    if (!allPaths.includes(jsonPath)) { errors.push(`sources has an unknown field path: ${jsonPath}`); continue; }
    if (!Array.isArray(list) || !list.length) { errors.push(`sources.${jsonPath} must be a nonempty array.`); continue; }
    for (const source of list) {
      const required = sourceTypes[source?.type];
      if (!required) errors.push(`sources.${jsonPath}: unknown source type ${JSON.stringify(source?.type)} (${Object.keys(sourceTypes).join(', ')}).`);
      else if (required.some(key => source[key] === undefined || source[key] === null || source[key] === '')) errors.push(`sources.${jsonPath}: ${source.type} needs ${required.join(', ')}.`);
      else if (source.type === 'official' && !/^https?:\/\//i.test(source.url)) errors.push(`sources.${jsonPath}: official url must be HTTP(S).`);
    }
  }
  for (const jsonPath of allPaths) {
    const value = get(draft, pointer(jsonPath));
    if (value !== null && value !== undefined && value !== '' && !sources[jsonPath]) errors.push(`${jsonPath} has a value but no source.`);
  }
  return errors;
}
function missingRequired(data) {
  return allPaths.filter(jsonPath => !optional.has(jsonPath)).filter(jsonPath => {
    const value = get({ data }, pointer(jsonPath));
    return value === null || value === undefined || (typeof value === 'string' && !value.trim() && !jsonPath.startsWith('data.taxonomies.'));
  });
}
// Cross-field checks a reviewer should resolve; none of them block the draft.
function consistencyWarnings(data, berths) {
  const warnings = [], f = data.vehicleFeatures || {}, spec = data.vehicleSpecifications || {}, icons = data.featuresIcons || {}, sf = data.taxonomies?.special_features || {};
  const pair = (a, b, label) => { if (typeof a === 'boolean' && typeof b === 'boolean' && a !== b) warnings.push(label); };
  pair(f.toilet, sf.toilet, 'vehicleFeatures.toilet and special feature Toilet disagree.');
  pair(f.shower, sf.shower, 'vehicleFeatures.shower and special feature Shower disagree.');
  pair(f.awning, sf.awning, 'vehicleFeatures.awning and special feature Awning disagree.');
  pair(f.bike_rack, sf.bike_rack, 'vehicleFeatures.bike_rack and special feature Bike Rack disagree.');
  pair(data.importantInformation?.pets_allowed, sf.pet_friendly, 'importantInformation.pets_allowed and special feature Pet Friendly disagree.');
  if (f.toilet === true && !icons.toilets) warnings.push('vehicleFeatures.toilet is true but the Toilets icon text is blank.');
  if (f.shower === true && !icons.showers) warnings.push('vehicleFeatures.shower is true but the Showers icon text is blank.');
  if (spec.transmission) {
    pair(/Automatic/.test(spec.transmission), sf.automatic_transmission, 'Transmission and special feature Automatic Transmission disagree.');
    pair(/Manual/.test(spec.transmission), sf.manual_transmission, 'Transmission and special feature Manual Transmission disagree.');
    if (icons.transmissions && normalize(icons.transmissions) !== normalize(spec.transmission)) warnings.push('featuresIcons.transmissions differs from vehicleSpecifications.transmission.');
  }
  if (spec.fuel_type === 'Diesel') { pair(true, sf.diesel, 'Fuel type Diesel but special feature Diesel is not set.'); pair(false, sf.petrol, 'Fuel type Diesel but special feature Petrol is set.'); }
  if (spec.fuel_type === 'Petrol') { pair(true, sf.petrol, 'Fuel type Petrol but special feature Petrol is not set.'); pair(false, sf.diesel, 'Fuel type Petrol but special feature Diesel is set.'); }
  if (icons.vehicle_ages && spec.vehicle_age && icons.vehicle_ages !== spec.vehicle_age) warnings.push('featuresIcons.vehicle_ages differs from vehicleSpecifications.vehicle_age.');
  const berthFlags = ['2_berth','3_berth','4_berth','5_berth','6_berth'].filter(key => sf[key] === true);
  if (berthFlags.length > 1) warnings.push(`More than one berth special feature is selected: ${berthFlags.join(', ')}.`);
  if (berths && berths >= 2 && berths <= 6 && sf[`${berths}_berth`] === false) warnings.push(`Catalog identity is ${berths} berth but special feature ${berths} Berth is not selected.`);
  if (sf['2wd'] === true && sf['4wd'] === true) warnings.push('Both 2WD and 4WD special features are selected.');
  if (!data.descriptionAndImages?.description) warnings.push('Description is blank. Generate it with the Paragraph generator before creation.');
  if (f.toilet === false && icons.toilets === 'Toilet') warnings.push('vehicleFeatures.toilet is false but the Toilets icon says Toilet.');
  if (f.shower === false && icons.showers === 'Shower') warnings.push('vehicleFeatures.shower is false but the Showers icon says Shower.');
  return warnings;
}

module.exports = { DRAFT_TYPE, SCHEMA, formFields, optional, addDefaults, blocking, selectOptions, brandTerms, locationTerms, countryRoot, specialFeatures, sourceTypes, normalize, pointer, allPaths, blankData, shapeErrors, valueErrors, sourceErrors, missingRequired, consistencyWarnings };
