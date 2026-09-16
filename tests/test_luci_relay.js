'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'package',
    'luci-app-doorfast', 'htdocs', 'luci-static', 'resources', 'view',
    'doorfast', 'relay.js'), 'utf8');

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
        assert.equal(configType, 'relay');
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
const relay = new Function('form', 'view', source)(form, view);
const rendered = relay.render();
const byName = name => rendered.options.find(option => option.optionName === name);

assert.equal(rendered.config, 'doorfast-events');
assert.deepEqual(rendered.options.map(option => option.optionName), [
    'enabled', 'url', 'entry_id', 'token', 'ca_file'
]);
assert.equal(byName('enabled').optionType, Flag);
assert.equal(byName('url').datatype, 'url');
assert.equal(byName('url').validate('main', 'https://ha.example:8443'), true);
assert.equal(byName('url').validate('main', 'http://ha.example:8123'), true);
assert.match(byName('url').validate('main', 'https://ha.example/path'), /路径/);
assert.equal(byName('entry_id').datatype, 'string');
assert.equal(byName('token').password, true);
assert.equal(byName('token').rmempty, true);
assert.equal(byName('token').cfgvalue('main'), '');
assert.equal(byName('ca_file').datatype, 'file');
assert.doesNotMatch(source, /require rpc|rpc\.declare|form\.Button/);
