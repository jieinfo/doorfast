'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'package',
    'luci-app-doorfast', 'htdocs', 'luci-static', 'resources', 'view',
    'doorfast', 'stations.js'), 'utf8');

function GridSection() {}
function Flag() {}
function Value() {}
function ListValue() {}
function DummyValue() {}

const stationData = {
    gate_existing: {
        '.name': 'gate_existing',
        '.type': 'station',
        enabled: '1',
        name: 'Existing Gate',
        logical_address: '32:02:01:00:03:00',
        ipv4: '10.2.1.30',
        route_preference: 'fixed',
        stream_name: 'doorfast_existing'
    }
};
const uciMutations = [];
let uciSaveCalls = 0;
let pendingChanges = {doorfast: [['set', 'gate_existing', 'unlock_mode', 'direct']]};
const uci = {
    changes: () => Promise.resolve(pendingChanges),
    load: config => Promise.resolve(config),
    add: (config, type, name) => {
        uciMutations.push(['add', config, type, name]);
        if (!Object.hasOwn(stationData, name)) {
            stationData[name] = {
                '.name': name,
                '.type': type
            };
        }
        return name;
    },
    set: (config, section, option, value) => {
        uciMutations.push(['set', config, section, option, value]);
        stationData[section][option] = value;
    },
    get: (config, section, option) => {
        const row = stationData[section];
        if (row === undefined)
            return null;
        return option === undefined ? row : (row[option] ?? null);
    },
    sections: (config, type) => Object.values(stationData).filter(
        section => section['.type'] === type),
    remove: (config, section) => {
        uciMutations.push(['remove', config, section]);
        delete stationData[section];
    },
    save: () => {
        uciSaveCalls += 1;
        return Promise.resolve();
    }
};

class Map {
    constructor(config) {
        this.config = config;
        this.options = [];
        this.sections = [];
        this.data = uci;
    }

    section(type, sectionType, title) {
        const map = this;
        const section = {
            type,
            sectionType,
            sectiontype: sectionType,
            title,
            map,
            options: [],
            modalSections: [],
            renderedRows: [],
            option: (optionType, optionName, label) => {
                const option = {
                    optionType,
                    optionName,
                    label,
                    disabled: '0',
                    enabled: '1',
                    formValues: {},
                    value: (value, valueLabel) => {
                        (option.values ??= []).push([value, valueLabel]);
                    },
                    formvalue: sectionId => Object.hasOwn(
                        option.formValues, sectionId)
                        ? option.formValues[sectionId]
                        : uci.get('doorfast', sectionId, optionName)
                };
                section.options.push(option);
                map.options.push(option);
                return option;
            },
            handleAdd: (event, name) => {
                const sectionId = map.data.add(
                    map.config, section.sectiontype, name);
                map.addedSection = sectionId;
                return section.renderMoreOptionsModal(sectionId);
            },
            renderMoreOptionsModal: sectionId => {
                assert.notEqual(uci.get('doorfast', sectionId), null);
                section.modalSections.push(sectionId);
                section.renderedRows.push({...uci.get('doorfast', sectionId)});
                return Promise.resolve();
            },
            handleModalCancel: (modalMap, event, isSaving) => {
                if (map.addedSection !== undefined && !isSaving)
                    map.data.remove(map.config, map.addedSection);
                delete map.addedSection;
                return Promise.resolve();
            },
            handleModalSave: () => uci.save().then(() => {
                delete map.addedSection;
            })
        };
        this.sections.push(section);
        return section;
    }

    render() {
        return Promise.resolve({
            type: 'station-map',
            rows: uci.sections('doorfast', 'station').map(
                section => section['.name'])
        });
    }

    save(callback, silent) {
        assert.equal(silent, true);
        if (this.invalid) return Promise.reject(new Error('invalid form'));
        return Promise.resolve().then(callback).then(() => {
            for (const row of Object.values(stationData)) {
                for (const option of this.options) {
                    if (option.optionName.startsWith('_')) continue;
                    const value = option.formvalue(row['.name']);
                    if (typeof value === 'string') row[option.optionName] = value;
                }
            }
            return uci.save();
        });
    }

    reset() { this.resets = (this.resets || 0) + 1; return this.render(); }
}

const form = {Map, GridSection, Flag, Value, ListValue, DummyValue};
const candidateUpdates = [];
const dom = {content: (node, content) => {
    candidateUpdates.push({node, content});
}};
const pollQueue = [];
const poll = {add: callback => {
    if (pollQueue.includes(callback))
        return false;
    pollQueue.push(callback);
    return true;
}};
const rpcCalls = [];
let scanError = null;
let credentialError = null;
let credentialFailureStation = null;
let applyError = null;
let applyCalls = 0;
let applyUpdates = true;
let deferApply = false;
let finishApply;
const eventListeners = new Set();
const fakeDocument = {
    addEventListener: (name, listener) => { assert.equal(name, 'uci-applied'); eventListeners.add(listener); },
    removeEventListener: (name, listener) => eventListeners.delete(listener),
    applied: () => [...eventListeners].forEach(listener => listener())
};
const pendingTimers = new globalThis.Map();
const credentialPayloads = {
    gate_existing: { configured: false, material_configured: true,
        mode: 'disabled', source: 'none', reason: 'disabled' }
};
const declarations = [];
const candidatesPayload = {candidates: [{
    logical_address: '32:02:01:00:02:00',
    ipv4: '10.2.1.20',
    first_seen_ms: 100,
    last_seen_ms: 200,
    reply_count: 2,
    configured: false
}]};
const rpc = {
    declare: declaration => { declarations.push(declaration); return (...args) => {
        rpcCalls.push({method: declaration.method, args});
        if (declaration.method === 'access_credentials_status')
            return Object.hasOwn(credentialPayloads, args[0])
                ? Promise.resolve(credentialPayloads[args[0]]) : Promise.reject(new Error('unknown station'));
        if (declaration.method === 'access_credentials_set' || declaration.method === 'access_credentials_clear') {
            if (credentialError && (!credentialFailureStation || credentialFailureStation === args[0]))
                return Promise.reject(credentialError);
            credentialPayloads[args[0]].material_configured = declaration.method === 'access_credentials_set';
            return Promise.resolve({...credentialPayloads[args[0]]});
        }
        if (declaration.method === 'station_scan' && scanError !== null)
            return Promise.reject(scanError);
        if (declaration.method === 'station_candidates')
            return Promise.resolve(candidatesPayload);
        return Promise.resolve({scheduled: true});
    }; }
};
const notifications = [];
const ui = {addNotification: (title, body, level) => {
    notifications.push({title, body, level});
}, changes: {apply: () => {
    applyCalls++;
    if (applyError) throw applyError;
    const complete = () => {
    if (applyUpdates) Object.keys(stationData).forEach(id => {
        const mode = stationData[id].unlock_mode || 'inherit';
        const material = credentialPayloads[id]?.material_configured || false;
        credentialPayloads[id] = { mode, material_configured: material,
            configured: mode === 'direct' && material,
            source: mode === 'direct' && material ? 'station' : 'none',
            reason: mode === 'disabled' ? 'disabled' : mode === 'direct' && material ? 'none' : 'material_missing' };
    });
    fakeDocument.applied();
    };
    if (deferApply) finishApply = complete;
    else queueMicrotask(complete);
    return undefined; // LuCI apply starts asynchronously and has no promise return.
}}};
const view = {extend: value => value};
const E = (tag, attrs, children) => ({tag, attrs, children});
const fakeWindow = {prompt: () => null,
    setTimeout: (callback, duration) => {
        assert.equal(duration, 120000);
        const key = {};
        pendingTimers.set(key, callback);
        return key;
    },
    clearTimeout: key => pendingTimers.delete(key)
};
const stations = new Function('dom', 'form', 'poll', 'rpc', 'uci', 'ui',
    'view', 'E', 'window', 'document', source)(dom, form, poll, rpc, uci, ui, view, E,
    fakeWindow, fakeDocument);

(async () => {
    const loaded = await stations.load();
    const page = {...stations};
    await page.render(loaded);
    await page.render(loaded);

    assert.equal(pollQueue.length, 1);
    assert.equal(page.stationSection.type, GridSection);
    assert.equal(page.stationSection.sectionType, 'station');
    assert.equal(page.stationSection.anonymous, false);
    assert.equal(page.stationSection.addremove, true);
    assert.deepEqual(page.stationSection.options.map(option => option.optionName), [
        'enabled', 'name', 'logical_address', 'ipv4',
        'route_preference', 'stream_name', 'unlock_mode', '_access_material', '_access_clear', '_access_status'
    ]);
    const byName = name => page.stationSection.options.find(
        option => option.optionName === name);
    assert.equal(byName('unlock_mode').validate('gate_existing', 'direct'), true);
    assert.notEqual(byName('unlock_mode').validate('gate_existing', 'bogus'), true);
    assert.equal(byName('_access_material').password, true);
    assert.equal(byName('_access_material').editable, true);
    assert.equal(byName('_access_material').modalonly, false);
    assert.equal(byName('_access_material').cfgvalue('gate_existing'), '');
    const mutations = uciMutations.length;
    byName('_access_material').write('gate_existing', '0011223344556677');
    byName('_access_material').remove('gate_existing');
    assert.equal(uciMutations.length, mutations);
    assert.deepEqual(byName('route_preference').values, [
        ['discover_first', '优先发现'], ['fixed', '固定地址']
    ]);

    const validateStationId = page.validateStationId;
    assert.equal(validateStationId('row', 'gate_main'), true);
    assert.match(validateStationId('row', 'Gate Main'), /小写/);

    assert.equal(byName('name').validate('gate_new', 'Main Gate'), true);
    assert.match(byName('name').validate('gate_new', ''), /名称/);
    assert.match(byName('name').validate('gate_new', '门口机'), /ASCII/);
    assert.match(byName('name').validate('gate_new', 'x'.repeat(65)), /64/);

    assert.equal(byName('ipv4').validate('gate_new', ''), true);
    assert.equal(byName('ipv4').validate('gate_new', '10.2.1.20'), true);
    for (const invalid of [
        '0.0.0.0', '0.1.2.3', '127.0.0.1', '224.0.0.1',
        '255.255.255.255', '10.2.1'
    ])
        assert.match(byName('ipv4').validate('gate_new', invalid), /IPv4/);
    byName('route_preference').formValues.gate_new = 'fixed';
    assert.match(byName('ipv4').validate('gate_new', ''), /固定/);
    byName('route_preference').formValues.gate_new = 'discover_first';

    assert.match(byName('logical_address').validate(
        'gate_new', '32:02:01:00:03:00'), /重复/);
    assert.match(byName('logical_address').validate(
        'gate_new', '32:02:01:00:0A:00'), /无效/);
    assert.match(byName('stream_name').validate(
        'gate_new', 'doorfast_existing'), /重复/);
    assert.equal(byName('stream_name').validate(
        'gate_new', 'doorfast_gate_new'), true);

    const beforeInvalidAdd = uciMutations.length;
    await page.stationSection.handleAdd(null, 'Gate Main');
    assert.equal(uciMutations.length, beforeInvalidAdd);
    assert.match(JSON.stringify(notifications.at(-1).body), /小写/);

    const existingSnapshot = {...stationData.gate_existing};
    const beforeDuplicateAdd = uciMutations.length;
    await page.stationSection.handleAdd(null, 'gate_existing');
    assert.equal(uciMutations.length, beforeDuplicateAdd);
    assert.deepEqual(stationData.gate_existing, existingSnapshot);
    assert.match(JSON.stringify(notifications.at(-1).body), /已存在/);

    await page.stationSection.handleAdd(null, 'gate_manual');
    assert.equal(page.stationMap.addedSection, 'gate_manual');
    assert.equal(page.stationSection.modalSections.at(-1), 'gate_manual');
    await page.stationSection.handleModalCancel(null, null, false);
    assert.equal(uci.get('doorfast', 'gate_manual'), null);
    assert.equal(uciSaveCalls, 0);

    const candidate = candidatesPayload.candidates[0];
    const beforeDuplicateAdopt = uciMutations.length;
    await page.adoptCandidate(candidate, 'gate_existing');
    assert.equal(uciMutations.length, beforeDuplicateAdopt);
    assert.deepEqual(stationData.gate_existing, existingSnapshot);
    assert.match(JSON.stringify(notifications.at(-1).body), /已存在/);

    await page.adoptCandidate(candidate, 'gate_main');
    assert.equal(uci.get('doorfast', 'gate_main', 'logical_address'),
        '32:02:01:00:02:00');
    assert.equal(uci.get('doorfast', 'gate_main', 'ipv4'), '10.2.1.20');
    assert.equal(uci.get('doorfast', 'gate_main', 'route_preference'),
        'discover_first');
    assert.equal(uci.get('doorfast', 'gate_main', 'stream_name'),
        'doorfast_gate_main');
    assert.equal(uciSaveCalls, 0);
    assert.equal(page.stationMap.addedSection, 'gate_main');
    assert.equal(page.stationSection.renderedRows.at(-1).stream_name,
        'doorfast_gate_main');
    await page.stationSection.handleModalCancel(null, null, false);
    assert.equal(uci.get('doorfast', 'gate_main'), null);
    assert.equal(uciSaveCalls, 0);

    await page.adoptCandidate(candidate, 'gate_main');
    await page.stationSection.handleModalSave();
    assert.notEqual(uci.get('doorfast', 'gate_main'), null);
    assert.equal(uciSaveCalls, 1);
    assert.equal(page.stationMap.addedSection, undefined);

    await page.handleScan();
    const scanCalls = rpcCalls.filter(call => call.method === 'station_scan');
    assert.deepEqual(scanCalls[0], {
        method: 'station_scan',
        args: []
    });
    assert.equal(candidateUpdates.length, 1);

    scanError = new Error('Permission denied');
    await page.handleScan();
    assert.equal(notifications.at(-1).level, 'danger');
    assert.match(JSON.stringify(notifications.at(-1).body), /Permission denied/);

    assert.deepEqual(declarations.find(x => x.method === 'access_credentials_set').params,
        ['station_id', 'material', 'mode']);
    assert.deepEqual(declarations.find(x => x.method === 'access_credentials_clear').params, ['station_id']);
    assert.deepEqual(declarations.find(x => x.method === 'access_credentials_status').params, ['station_id']);
    const secretWrites = () => rpcCalls.filter(x => ['access_credentials_set', 'access_credentials_clear'].includes(x.method));
    const original = {...stationData.gate_existing};
    byName('unlock_mode').formValues.gate_existing = 'direct';
    byName('_access_material').formValues.gate_existing = '';
    byName('_access_clear').formValues.gate_existing = '0';
    deferApply = true;
    let applySettled = false;
    let firstError;
    const firstApply = page.handleSaveApply().catch(error => { firstError = error; })
        .finally(() => { applySettled = true; });
    await new Promise(setImmediate);
    assert.equal(applyCalls, 1);
    assert.equal(firstError, undefined);
    assert.equal(applySettled, false);
    finishApply();
    await firstApply;
    assert.equal(firstError, undefined);
    deferApply = false;
    assert.equal(page.stationMap.resets, 1);
    assert.equal(eventListeners.size, 0);
    assert.equal(pendingTimers.size, 0);
    assert.equal(secretWrites().length, 0); // retained direct record despite effective disabled=false
    assert.equal(credentialPayloads.gate_existing.configured, true);
    assert.equal(stationData.gate_existing.unlock_mode, 'direct');
    assert.deepEqual({...stationData.gate_existing, unlock_mode: undefined}, {...original, unlock_mode: undefined});
    pendingChanges = {};
    const beforeNoChangeApply = applyCalls;
    await page.handleSaveApply();
    assert.equal(applyCalls, beforeNoChangeApply);
    byName('_access_material').formValues.gate_existing = '0011223344556677';
    await page.handleSaveApply();
    assert.equal(applyCalls, beforeNoChangeApply);
    assert.deepEqual(secretWrites().at(-1).args, ['gate_existing', '0011223344556677', 'direct']);
    byName('_access_material').formValues.gate_existing = '';
    pendingChanges = {network: [['set', 'lan', 'mtu', '1500']]};
    await page.handleSaveApply();
    assert.equal(applyCalls, beforeNoChangeApply + 1);
    assert.equal(eventListeners.size, 0);
    assert.equal(pendingTimers.size, 0);
    const beforeErrorSave = uciSaveCalls;
    const beforeErrorApply = applyCalls;
    byName('_access_material').formValues.gate_existing = '0011223344556677';
    credentialError = new Error('0011223344556677 sensitive backend error');
    await assert.rejects(page.handleSaveApply(), /station save\/apply failed/);
    assert.equal(uciSaveCalls, beforeErrorSave);
    assert.equal(applyCalls, beforeErrorApply);
    assert.equal(byName('_access_material').formvalue('gate_existing'), '0011223344556677');
    assert.equal(JSON.stringify(notifications.at(-1)).includes('0011223344556677'), false);
    assert.deepEqual(secretWrites().at(-1).args, ['gate_existing', '0011223344556677', 'direct']);
    credentialError = null;
    byName('_access_clear').formValues.gate_existing = '1';
    const beforeConflict = secretWrites().length;
    await assert.rejects(page.handleSaveApply(), /validation/);
    assert.equal(secretWrites().length, beforeConflict);
    byName('_access_material').formValues.gate_existing = '';
    await page.handleSaveApply();
    assert.deepEqual(secretWrites().at(-1), {method: 'access_credentials_clear', args: ['gate_existing']});
    assert.equal(stationData.gate_existing.unlock_mode, 'direct');
    assert.equal(credentialPayloads.gate_existing.configured, false);
    assert.match(page.accessStatus.cfgvalue('gate_existing'), /缺少开锁材料/);
    byName('_access_clear').formValues.gate_existing = '0';
    byName('_access_material').formValues.gate_main = '8899aabbccddeeff';
    delete credentialPayloads.gate_main;
    const beforeUnknown = secretWrites().length;
    await assert.rejects(page.handleSaveApply());
    assert.equal(secretWrites().length, beforeUnknown);
    assert.match(JSON.stringify(notifications.at(-1)), /先保存并应用/);
    byName('_access_material').formValues.gate_main = '';
    await page.handleSaveApply(); // creation without secret, explicit unavailable metadata
    assert.match(page.accessStatus.cfgvalue('gate_main'), /缺少开锁材料/);
    byName('_access_material').formValues.gate_existing = '0011223344556677';
    byName('unlock_mode').formValues.gate_main = 'bogus';
    const beforeInvalidRow = secretWrites().length;
    await assert.rejects(page.handleSaveApply());
    assert.equal(secretWrites().length, beforeInvalidRow);
    byName('unlock_mode').formValues.gate_main = '';
    await assert.rejects(page.handleSaveApply());
    assert.equal(secretWrites().length, beforeInvalidRow);
    byName('unlock_mode').formValues.gate_main = 'inherit';
    page.stationMap.invalid = true;
    await assert.rejects(page.handleSaveApply());
    assert.equal(secretWrites().length, beforeInvalidRow);
    page.stationMap.invalid = false;
    credentialPayloads.gate_existing = {configured: false, material_configured: false,
        mode: 'direct', source: 'none', reason: 'secret detail'};
    await assert.rejects(page.handleSaveApply());
    assert.equal(secretWrites().length, beforeInvalidRow);
    credentialPayloads.gate_existing = {configured: true}; // old/unsupported bridge
    await assert.rejects(page.handleSaveApply());
    assert.equal(secretWrites().length, beforeInvalidRow);
    credentialPayloads.gate_existing = {configured: false, material_configured: false,
        mode: 'direct', source: 'none', reason: 'material_missing'};
    byName('_access_material').formValues.gate_existing = '';
    applyError = new Error('sensitive apply text');
    await assert.rejects(page.handleSaveApply());
    assert.equal(JSON.stringify(notifications.at(-1)).includes('sensitive'), false);
    applyError = null;
    applyUpdates = false;
    byName('unlock_mode').formValues.gate_existing = 'disabled';
    await assert.rejects(page.handleSaveApply()); // runtime still reports direct
    assert.match(JSON.stringify(notifications.at(-1)), /gate_existing/);
    assert.match(JSON.stringify(notifications.at(-1)), /配置已保存/);
    deferApply = true;
    const timedApply = page.handleSaveApply();
    const timedResult = assert.rejects(timedApply, /station save\/apply failed/);
    await new Promise(setImmediate);
    assert.equal(pendingTimers.size, 1);
    assert.equal(eventListeners.size, 1);
    [...pendingTimers.values()].forEach(callback => callback());
    await timedResult;
    assert.equal(pendingTimers.size, 0);
    assert.equal(eventListeners.size, 0);
    deferApply = false;
    assert.equal(page.credentialStatus({configured: false}), null);
    assert.equal(page.credentialStatus({configured: false, material_configured: true,
        mode: 'inherit', source: 'none', reason: 'secret detail'}).reason, 'unknown');
    applyUpdates = true;
    byName('unlock_mode').formValues.gate_existing = 'direct';
    byName('_access_material').formValues.gate_existing = '0011223344556677';
    byName('_access_material').formValues.gate_main = '8899aabbccddeeff';
    credentialError = new Error('private-material-error');
    credentialFailureStation = 'gate_main';
    const beforePartialSave = uciSaveCalls;
    const beforePartialApply = applyCalls;
    await assert.rejects(page.handleSaveApply());
    assert.equal(uciSaveCalls, beforePartialSave);
    assert.equal(applyCalls, beforePartialApply);
    assert.equal(credentialPayloads.gate_existing.material_configured, true);
    assert.match(JSON.stringify(notifications.at(-1)), /已更新独立材料：gate_existing/);
    assert.match(JSON.stringify(notifications.at(-1)), /gate_main/);
    assert.equal(JSON.stringify(notifications.at(-1)).includes('private-material'), false);
})();
