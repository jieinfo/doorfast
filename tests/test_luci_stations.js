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
const uci = {
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
}

const form = {Map, GridSection, Flag, Value, ListValue};
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
const candidatesPayload = {candidates: [{
    logical_address: '32:02:01:00:02:00',
    ipv4: '10.2.1.20',
    first_seen_ms: 100,
    last_seen_ms: 200,
    reply_count: 2,
    configured: false
}]};
const rpc = {
    declare: declaration => (...args) => {
        rpcCalls.push({method: declaration.method, args});
        if (declaration.method === 'station_scan' && scanError !== null)
            return Promise.reject(scanError);
        if (declaration.method === 'station_candidates')
            return Promise.resolve(candidatesPayload);
        return Promise.resolve({scheduled: true});
    }
};
const notifications = [];
const ui = {addNotification: (title, body, level) => {
    notifications.push({title, body, level});
}};
const view = {extend: value => value};
const E = (tag, attrs, children) => ({tag, attrs, children});
const fakeWindow = {prompt: () => null};
const stations = new Function('dom', 'form', 'poll', 'rpc', 'uci', 'ui',
    'view', 'E', 'window', source)(dom, form, poll, rpc, uci, ui, view, E,
    fakeWindow);

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
        'route_preference', 'stream_name'
    ]);
    const byName = name => page.stationSection.options.find(
        option => option.optionName === name);
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
})();
