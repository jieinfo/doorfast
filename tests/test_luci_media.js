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

const saveOrder = [];

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
        saveOrder.push('uci');
        return Promise.resolve(callback()).then(() => {
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
const rpcCalls = [];
const rpc = {
    declare: declaration => {
        rpcDeclarations.push(declaration);
        return (...args) => {
            saveOrder.push(declaration.method);
            rpcCalls.push({method: declaration.method, args});
            if (declaration.method === 'status')
                return Promise.resolve({media: {installed: true}});
            return Promise.resolve({});
        };
    }
};
const ui = {changes: {apply: () => {
    saveOrder.push('apply');
    return Promise.resolve();
}}};
const view = {extend: value => value};
const E = () => ({});
const media = new Function('form', 'rpc', 'ui', 'view', 'E', source)(
    form, rpc, ui, view, E);

const page = {...media};

(async () => {
    for (const unavailablePayload of [
        {media: {installed: false}}, {}, null
    ]) {
        const unavailablePage = {...media};
        await unavailablePage.render(unavailablePayload);
        assert.equal(unavailablePage.mediaMap, null);
    }
    await page.render({media: {installed: true}});
    const byName = name => page.mediaMap.options.find(option =>
        option.optionName === name);

    assert.equal(page.mediaMap.config, 'doorfast');
    assert.deepEqual(page.mediaMap.options.map(option => option.optionName), [
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
    page.rtspPassword.formvalue = () => 'new-password';
    page.relayToken.formvalue = () => '';
    page.clearRtspPassword.formvalue = () => '0';
    page.clearRelayToken.formvalue = () => '1';
    await page.saveMedia(true);
    assert.deepEqual(rpcCalls.find(call => call.method === 'media_credentials'), {
        method: 'media_credentials',
        args: ['new-password', '', false, true]
    });
    assert.deepEqual(saveOrder.slice(-3), ['uci', 'media_credentials', 'apply']);
})();
