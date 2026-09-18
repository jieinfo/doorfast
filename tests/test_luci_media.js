'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'package',
    'luci-app-doorfast', 'htdocs', 'luci-static', 'resources', 'view',
    'doorfast', 'media.js'), 'utf8');
const statusSource = fs.readFileSync(path.join(__dirname, '..', 'package',
    'luci-app-doorfast', 'htdocs', 'luci-static', 'resources', 'view',
    'doorfast', 'status.js'), 'utf8');

function NamedSection() {}
function Flag() {}
function Value() {}
function ListValue() {}

let transactionLog = [];
let uciFailure = null;
let credentialFailure = null;
let applyFailure = null;
let rpcCalls = [];
let notifications = [];

class Map {
    constructor(config) {
        this.config = config;
        this.options = [];
    }

    section(type, name, configType, title) {
        assert.equal(type, NamedSection);
        assert.equal(name, 'main');
        assert.equal(configType, 'gvs');
        return {
            option: (optionType, optionName, label) => {
                const option = {
                    optionType, optionName, label, title, disabled: '0',
                    depends: (name, value) => {
                        option.dependency = {name, value};
                    },
                    value: (value, label) => {
                        (option.values ??= []).push([value, label]);
                    },
                    formvalue: () => ''
                };
                this.options.push(option);
                return option;
            }
        };
    }

    render() {
        return Promise.resolve({});
    }

    save(callback) {
        transactionLog.push('parse');
        return Promise.resolve()
            .then(callback)
            .then(() => {
                transactionLog.push('uci');
                if (uciFailure !== null)
                    throw uciFailure;
                this.options.forEach(option => {
                    option.formvalue = () => option.disabled;
                });
            });
    }

    reset() {
        return Promise.resolve();
    }
}

const form = {Map, NamedSection, Flag, Value, ListValue};
const rpcDeclarations = [];
const rpc = {
    declare: declaration => {
        rpcDeclarations.push(declaration);
        return (...args) => {
            rpcCalls.push({method: declaration.method, args});
            if (declaration.method === 'status')
                return Promise.resolve({media: {installed: true}});
            transactionLog.push(declaration.method);
            if (credentialFailure !== null)
                return Promise.reject(credentialFailure);
            return Promise.resolve({});
        };
    }
};
const ui = {
    changes: {apply: () => {
        transactionLog.push('apply');
        return applyFailure === null
            ? Promise.resolve() : Promise.reject(applyFailure);
    }},
    addNotification: (title, body, level) => {
        notifications.push({title, body, level});
    }
};
const view = {extend: value => value};
const E = (tag, attrs, children) => ({tag, attrs, children});
const media = new Function('form', 'rpc', 'ui', 'view', 'E', source)(
    form, rpc, ui, view, E);

function resetTransaction() {
    transactionLog = [];
    rpcCalls = [];
    notifications = [];
    uciFailure = null;
    credentialFailure = null;
    applyFailure = null;
}

async function renderPage(values) {
    const page = {...media};

    await page.render({media: {installed: true}});
    const byName = name => page.mediaMap.options.find(option =>
        option.optionName === name);
    Object.entries(values ?? {}).forEach(([name, value]) => {
        byName(name).formvalue = () => value;
    });
    return {page, byName};
}

(async () => {
    for (const unavailablePayload of [
        {media: {installed: false}}, {}, null
    ]) {
        const unavailablePage = {...media};
        await unavailablePage.render(unavailablePayload);
        assert.equal(unavailablePage.mediaMap, null);
    }

    let rendered = await renderPage();
    const byName = rendered.byName;
    assert.equal(rendered.page.mediaMap.config, 'doorfast');
    assert.deepEqual(rendered.page.mediaMap.options.map(
        option => option.optionName), [
        'media_enabled', 'media_station_address', 'media_station_ipv4',
        'media_go2rtc_host', 'media_go2rtc_port', 'media_stream_name',
        'media_rtsp_username', 'media_encoder', 'media_resolution',
        'media_fps', 'media_bitrate_kbps', 'media_profile',
        'media_min_free_kib', 'media_preview_timeout',
        'media_first_frame_timeout', 'media_relay_url',
        '_media_rtsp_password', '_media_clear_rtsp_password',
        '_media_relay_token', '_media_clear_relay_token'
    ]);
    assert.equal(byName('media_enabled').optionType, Flag);
    assert.equal(byName('media_station_address').validate(
        'main', '32:02:01:00:02:00'), true);
    assert.match(byName('media_station_address').validate(
        'main', '32:02:01:00:00:00'), /无效/);
    assert.equal(byName('media_go2rtc_host').validate('main', 'ha.local'), true);
    assert.match(byName('media_go2rtc_host').validate(
        'main', 'https://ha.local'), /协议/);
    assert.deepEqual(byName('media_fps').values.map(value => value[0]),
        ['5', '8', '10', '12', '15']);
    assert.equal(byName('_media_rtsp_password').password, true);
    assert.equal(byName('_media_rtsp_password').cfgvalue('main'), '');
    assert.equal(byName('_media_rtsp_password').write('main', ''), undefined);
    assert.equal(byName('_media_relay_token').password, true);
    assert.equal(byName('_media_clear_rtsp_password').cfgvalue('main'), '0');
    assert.equal(byName('_media_clear_relay_token').cfgvalue('main'), '0');
    assert.equal(byName('media_relay_url').validate(
        'main', 'https://ha.local/api/doorfast/entry-1'), true);
    assert.match(byName('media_relay_url').validate(
        'main', 'https://ha.local/api/doorfast/entry-1?x=1'), /query/);
    assert.equal(source.includes("'media_rtsp_password'"), false);
    assert.equal(source.includes("'media_relay_token'"), false);
    assert.doesNotMatch(statusSource, /media_enabled|media_credentials/);

    const credentialsDeclaration = rpcDeclarations.find(declaration =>
        declaration.method === 'media_credentials');
    assert.deepEqual(credentialsDeclaration.params, [
        'rtsp_password', 'relay_token', 'clear_rtsp_password',
        'clear_relay_token'
    ]);

    resetTransaction();
    rendered = await renderPage({
        _media_rtsp_password: 'new-password',
        _media_clear_relay_token: '1'
    });
    await rendered.page.saveMedia(true);
    assert.deepEqual(rpcCalls.find(call => call.method === 'media_credentials'), {
        method: 'media_credentials',
        args: ['new-password', '', false, true]
    });
    assert.deepEqual(transactionLog,
        ['parse', 'uci', 'media_credentials', 'apply']);

    resetTransaction();
    rendered = await renderPage();
    await rendered.page.saveMedia(true);
    assert.equal(rpcCalls.some(call => call.method === 'media_credentials'), false);
    assert.deepEqual(transactionLog, ['parse', 'uci', 'apply']);

    resetTransaction();
    rendered = await renderPage({_media_clear_rtsp_password: '1'});
    await rendered.page.saveMedia(false);
    assert.deepEqual(rpcCalls.at(-1), {
        method: 'media_credentials', args: ['', '', true, false]
    });
    assert.deepEqual(transactionLog, ['parse', 'uci', 'media_credentials']);

    resetTransaction();
    rendered = await renderPage({_media_clear_relay_token: '1'});
    await rendered.page.saveMedia(false);
    assert.deepEqual(rpcCalls.at(-1), {
        method: 'media_credentials', args: ['', '', false, true]
    });

    for (const conflict of [
        {
            _media_rtsp_password: 'new-password',
            _media_clear_rtsp_password: '1'
        },
        {
            _media_relay_token: 'new-token',
            _media_clear_relay_token: '1'
        }
    ]) {
        resetTransaction();
        rendered = await renderPage(conflict);
        await assert.rejects(rendered.page.saveMedia(true), /不能同时填写并清除/);
        assert.deepEqual(transactionLog, []);
        assert.equal(rpcCalls.length, 0);
    }

    resetTransaction();
    rendered = await renderPage({_media_rtsp_password: 'new-password'});
    uciFailure = new Error('UCI save failed');
    await assert.rejects(rendered.page.saveMedia(true), /UCI save failed/);
    assert.deepEqual(transactionLog, ['parse', 'uci']);
    assert.equal(rpcCalls.length, 0);

    resetTransaction();
    rendered = await renderPage({_media_relay_token: 'new-token'});
    credentialFailure = new Error('credential write failed');
    await assert.rejects(rendered.page.saveMedia(true), /credential write failed/);
    assert.deepEqual(transactionLog, ['parse', 'uci', 'media_credentials']);
    assert.equal(transactionLog.includes('apply'), false);
    assert.equal(notifications.at(-1).level, 'danger');
    assert.match(JSON.stringify(notifications.at(-1).body),
        /credential write failed/);
})();
