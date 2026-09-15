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

class Map {
    constructor(config) {
        this.config = config;
        this.options = [];
    }

    section(type, name, configType) {
        assert.equal(type, NamedSection);
        assert.equal(name, 'main');
        assert.equal(configType, 'gvs');
        return {
            option: (optionType, optionName, label) => {
                const option = {optionType, optionName, label, disabled: '0'};
                this.options.push(option);
                return option;
            }
        };
    }

    render() {
        return this;
    }
}

const form = {Map, NamedSection, Flag, Value};
const view = {extend: value => value};
const deployment = new Function('form', 'view', source)(form, view);
const rendered = deployment.render();
const byName = name => rendered.options.find(option => option.optionName === name);

assert.equal(rendered.config, 'doorfast');
assert.deepEqual(rendered.options.map(option => option.optionName), [
    'enabled', 'active_host', 'gvs_interface',
    'uplink_interface', 'gvs_local_address', 'access_material'
]);
assert.equal(byName('enabled').optionType, Flag);
assert.equal(byName('active_host').optionType, Flag);
assert.equal(byName('gvs_interface').optionType, Value);
assert.equal(byName('uplink_interface').optionType, Value);
assert.equal(byName('gvs_local_address').datatype, 'string');
assert.equal(byName('access_material').password, true);
assert.equal(byName('access_material').datatype, 'hexstring');
assert.equal(byName('access_material').rmempty, true);
assert.equal(Object.hasOwn(byName('access_material'), 'default'), false);
assert.equal(byName('access_material').validate('main', ''), true);
assert.equal(byName('access_material').validate('main', '0D753EA99003CD5D'), true);
assert.match(byName('access_material').validate('main', '0d753ea9'), /16 位/);
assert.match(byName('access_material').validate('main', '0d753ea99003cd5z'), /十六进制/);
assert.match(byName('active_host').description, /主机模式/);
assert.doesNotMatch(source, /require rpc|rpc\.declare|form\.Button/);
