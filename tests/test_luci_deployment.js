'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'package',
    'luci-app-doorfast', 'htdocs', 'luci-static', 'resources', 'view',
    'doorfast', 'deployment.js'), 'utf8');

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
                        option.dependency = typeof name === 'object'
                            ? name : {name, value};
                    },
                    value: (value, label) => {
                        (option.values ??= []).push([value, label]);
                    }
                };
                this.options.push(option);
                return option;
            }
        };
    }

    render() {
        return this;
    }
}

const form = {Map, NamedSection, Flag, Value, ListValue};
const view = {extend: value => value};
const deployment = new Function('form', 'view', source)(form, view);
const rendered = deployment.render();
const byName = (name, title) => rendered.options.find(option =>
    option.optionName === name && option.title === title);

assert.equal(rendered.config, 'doorfast');
const passive = '被动观察配置';
const host = '主机模式配置';
assert.equal(byName('enabled', '主服务与模式').optionType, Flag);
assert.equal(byName('active_host', '主服务与模式').optionType, Flag);
assert.equal(byName('passive_interface', passive).label, '被动观察接口');
assert.equal(byName('passive_interface', passive).dependency.value, '0');
assert.equal(byName('capture_promiscuous', passive).optionType, Flag);
assert.equal(byName('capture_promiscuous', passive).dependency.value, '0');
assert.equal(byName('gvs_local_address', passive).datatype, 'string');
assert.match(byName('gvs_local_address', passive).description, /不绑定室内机 IP/);
assert.equal(byName('host_interface', host).label, '主机模式接口');
assert.equal(byName('host_interface', host).dependency.value, '1');
assert.equal(byName('gvs_local_address', host).label, 'GVS 逻辑身份');
assert.equal(byName('gvs_local_address', host).validate('main', 'IS:2-1-101-1'), true);
assert.match(byName('gvs_local_address', host).validate('main', 'IS:2-1-133-1'), /房间/);
assert.equal(byName('indoor_ipaddr', host).datatype, 'ip4addr');
assert.equal(byName('indoor_ipaddr', host).rmempty, true);
assert.match(byName('indoor_ipaddr', host).description, /自动推导/);
assert.equal(byName('indoor_netmask', host).datatype, 'ip4addr');
assert.equal(byName('indoor_netmask', host).rmempty, true);
assert.equal(byName('access_material', host).password, true);
assert.equal(byName('access_material', host).datatype, 'hexstring');
assert.equal(byName('access_material', host).rmempty, true);
assert.equal(Object.hasOwn(byName('access_material', host), 'default'), false);
assert.equal(byName('access_material', host).validate('main', ''), true);
    assert.equal(byName('access_material', host).validate('main', '0D753EA99003CD5D'), true);
assert.match(byName('access_material', host).validate('main', '0d753ea9'), /16 位/);
    assert.match(byName('access_material', host).validate('main', '0d753ea99003cd5z'), /十六进制/);
    assert.equal(byName('sync_mini1_secretkey', host).password, true);
    assert.equal(byName('sync_mini1_secretkey', host).rmempty, true);
    assert.equal(byName('sync_mini1_secretkey', host).dependency.value, '1');
    assert.equal(byName('sync_mini2_secretkey', host).password, true);
    assert.equal(byName('sync_mini2_secretkey', host).rmempty, true);
    assert.match(byName('sync_mini2_secretkey', host).description, /91\/03/);
assert.equal(byName('uplink_interface', '可选上行配置').optionType, Value);
assert.equal(byName('multicast_mode', host).optionType, ListValue);
assert.deepEqual(byName('multicast_mode', host).values, [
    ['auto', '自动推导'], ['custom', '自定义']
]);
assert.equal(byName('multicast_mode', host).dependency.value, '1');
assert.equal(byName('multicast_address', host).datatype, 'ip4addr');
assert.deepEqual(byName('multicast_address', host).dependency, {
    active_host: '1', multicast_mode: 'custom'
});
assert.equal(byName('multicast_address', host).validate(
    'main', '224.0.0.0'), true);
assert.equal(byName('multicast_address', host).validate(
    'main', '239.255.255.255'), true);
assert.match(byName('multicast_address', host).validate(
    'main', '223.255.255.255'), /组播/);
assert.match(byName('multicast_address', host).validate(
    'main', '240.0.0.0'), /组播/);
assert.match(byName('multicast_address', host).validate(
    'main', '192.168.1.1'), /组播/);
assert.match(byName('multicast_address', host).validate(
    'main', '239.1.2'), /IPv4/);
assert.doesNotMatch(source, /require rpc|rpc\.declare|form\.Button/);
