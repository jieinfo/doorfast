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

const uciMutations = [];
let uciSaveCalls = 0;
const uci = {
    load: config => Promise.resolve(config),
    add: (config, type, name) => {
        uciMutations.push(['add', config, type, name]);
        return name;
    },
    set: (config, section, option, value) => {
        uciMutations.push(['set', config, section, option, value]);
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
        const section = {
            type, sectionType, title, options: [],
            option: (optionType, optionName, label) => {
                const option = {
                    optionType, optionName, label, disabled: '0',
                    value: (value, valueLabel) => {
                        (option.values ??= []).push([value, valueLabel]);
                    }
                };
                section.options.push(option);
                this.options.push(option);
                return option;
            },
            renderMoreOptionsModal: sectionId => {
                section.modalSection = sectionId;
                return Promise.resolve();
            }
        };
        this.sections.push(section);
        return section;
    }

    render() {
        return Promise.resolve({type: 'station-map'});
    }
}

const form = {Map, GridSection, Flag, Value, ListValue};
const candidateUpdates = [];
const dom = {content: (node, content) => {
    candidateUpdates.push({node, content});
}};
const pollCallbacks = [];
const poll = {add: callback => pollCallbacks.push(callback)};
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

    assert.equal(page.stationSection.type, GridSection);
    assert.equal(page.stationSection.sectionType, 'station');
    assert.equal(page.stationSection.anonymous, false);
    assert.equal(page.stationSection.addremove, true);
    assert.deepEqual(page.stationSection.options.map(option => option.optionName), [
        'enabled', 'name', 'logical_address', 'ipv4',
        'route_preference', 'stream_name'
    ]);
    assert.deepEqual(page.stationSection.options.find(option =>
        option.optionName === 'route_preference').values, [
        ['discover_first', '优先发现'], ['fixed', '固定地址']
    ]);

    const validateStationId = page.validateStationId;
    assert.equal(validateStationId('row', 'gate_main'), true);
    assert.match(validateStationId('row', 'Gate Main'), /小写/);

    await page.handleScan();
    const scanCalls = rpcCalls.filter(call => call.method === 'station_scan');
    assert.deepEqual(scanCalls[0], {
        method: 'station_scan',
        args: []
    });
    assert.equal(candidateUpdates.length, 1);
    assert.equal(pollCallbacks.length, 1);

    const candidate = candidatesPayload.candidates[0];
    await page.adoptCandidate(candidate, 'gate_main');
    assert.deepEqual(uciMutations, [
        ['add', 'doorfast', 'station', 'gate_main'],
        ['set', 'doorfast', 'gate_main', 'enabled', '1'],
        ['set', 'doorfast', 'gate_main', 'name', 'gate_main'],
        ['set', 'doorfast', 'gate_main', 'logical_address',
            '32:02:01:00:02:00'],
        ['set', 'doorfast', 'gate_main', 'ipv4', '10.2.1.20'],
        ['set', 'doorfast', 'gate_main', 'route_preference', 'discover_first'],
        ['set', 'doorfast', 'gate_main', 'stream_name',
            'doorfast_gate_main']
    ]);
    assert.equal(uciSaveCalls, 0);
    assert.equal(page.stationSection.modalSection, 'gate_main');

    scanError = new Error('Permission denied');
    await page.handleScan();
    assert.equal(notifications.at(-1).level, 'danger');
    assert.match(JSON.stringify(notifications.at(-1).body), /Permission denied/);
})();
