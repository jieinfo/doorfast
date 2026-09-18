'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'package',
    'luci-app-doorfast', 'htdocs', 'luci-static', 'resources', 'view',
    'doorfast', 'status.js'), 'utf8');

function NamedSection() {}
function Flag() {}
function Value() {}
function ListValue() {}

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
        if (typeof callback !== 'function') {
            this.options.forEach(option => {
                option.formvalue = () => option.disabled;
            });
            return Promise.resolve();
        }
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
const dom = {content: () => {}};
const poll = {add: () => {}};
const rpcDeclarations = [];
const rpcCalls = [];
const rpc = {
    declare: declaration => {
        rpcDeclarations.push(declaration);
        return (...args) => {
            rpcCalls.push({method: declaration.method, args});
            return Promise.resolve({});
        };
    }
};
const ui = {changes: {apply: () => Promise.resolve()}};
const view = {extend: value => value};
const statusModel = {
    unavailableLabel: 'unavailable', staleLabel: 'stale',
    formatStatus: () => []
};
const E = () => ({});
const dashboard = new Function('dom', 'form', 'poll', 'rpc', 'ui', 'view',
    'statusModel', 'E', source)(dom, form, poll, rpc, ui, view, statusModel, E);

const payload = {media: {installed: true}};
const page = {...dashboard};

(async () => {
    for (const unavailablePayload of [
        {media: {installed: false}}, {}, null
    ]) {
        const unavailablePage = {...dashboard};
        await unavailablePage.render(unavailablePayload);
        assert.equal(unavailablePage.mediaMap, null);
    }
    await page.render(payload);
    const byName = name => page.mediaMap.options.find(option =>
        option.optionName === name);

    assert.equal(page.mediaMap.config, 'doorfast');
    assert.equal(byName('media_enabled').optionType, Flag);
    assert.equal(byName('media_enabled').dependency, undefined);
    assert.equal(byName('media_station_address').dependency, undefined);
    assert.equal(byName('_media_rtsp_password').dependency, undefined);
    assert.equal(byName('media_station_address').validate(
        'main', '32:02:01:00:02:00'), true);
    assert.match(byName('media_station_address').validate(
        'main', '32:00:01:00:02:00'), /无效/);
    assert.match(byName('media_station_address').validate(
        'main', '32:02:00:00:02:00'), /无效/);
    assert.match(byName('media_station_address').validate(
        'main', '32:1a:01:00:02:00'), /无效/);
    assert.match(byName('media_station_address').validate(
        'main', '32:02:01:00:00:00'), /无效/);
    assert.match(byName('media_station_address').validate(
        'main', '32:02:01:00:1a:00'), /无效/);
    assert.match(byName('media_station_address').validate(
        'main', 'IS:2-1-101-1'), /32:bb:uu/);
    assert.equal(byName('media_go2rtc_host').validate('main', 'ha.local'), true);
    assert.match(byName('media_go2rtc_host').validate(
        'main', 'https://ha.local'), /协议/);
    assert.deepEqual(byName('media_fps').values.map(value => value[0]),
        ['5', '8', '10', '12', '15']);
    assert.equal(byName('media_min_free_kib').validate(
        'main', '393216'), true);
    assert.equal(byName('media_preview_timeout').validate(
        'main', '120'), true);
    assert.equal(byName('media_first_frame_timeout').validate(
        'main', '8'), true);
    [
        'media_max_encoders', 'media_overload_policy', 'media_diagnostics',
        'media_publish_retries'
    ].forEach(name => assert.equal(byName(name), undefined));
    assert.equal(byName('_media_rtsp_password').password, true);
    assert.equal(byName('_media_rtsp_password').cfgvalue('main'), '');
    assert.equal(byName('_media_rtsp_password').write('main', ''), undefined);
    assert.equal(byName('_media_relay_token').password, true);
    assert.equal(byName('_media_clear_rtsp_password').cfgvalue('main'), '0');
    assert.equal(byName('_media_clear_relay_token').cfgvalue('main'), '0');
    assert.equal(byName('media_relay_url').validate(
        'main', 'https://ha.local:8123'), true);
    assert.equal(byName('media_relay_url').validate(
        'main', 'http://ha.local:65535'), true);
    assert.match(byName('media_relay_url').validate(
        'main', 'http://ha.local:65536'), /端口/);
    assert.equal(byName('media_relay_url').validate(
        'main', 'https://ha.local/api/doorfast/entry-1'), true);
    assert.match(byName('media_relay_url').validate(
        'main', 'https://ha.local/api/doorfast/entry-1?x=1'), /query/);
    assert.match(byName('media_relay_url').validate(
        'main', 'https://ha.local/api/doorfast/entry-1#fragment'), /fragment/);
    assert.equal(source.includes("'media_rtsp_password'"), false);
    assert.equal(source.includes("'media_relay_token'"), false);

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
    await page.saveMedia(false);
    assert.deepEqual(rpcCalls.find(call => call.method === 'media_credentials'), {
        method: 'media_credentials',
        args: ['new-password', '', false, true]
    });
})();
