'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'package',
    'luci-app-doorfast', 'htdocs', 'luci-static', 'resources', 'view',
    'doorfast', 'settings.js'), 'utf8');

function NamedSection() {}
function Flag() {}

class Map {
    constructor(config, title, description) {
        this.config = config;
        this.title = title;
        this.description = description;
    }

    section(type, name, configType, title) {
        assert.equal(type, NamedSection);
        assert.equal(name, 'main');
        assert.equal(configType, 'automation');
        assert.equal(title, '来电自动化');
        return {
            option: (optionType, optionName, label) => {
                assert.equal(optionType, Flag);
                assert.equal(optionName, 'call_elev');
                assert.equal(label, '来电自动向上召梯');
                this.option = {disabled: '0'};
                return this.option;
            }
        };
    }

    render() {
        return this;
    }
}

const form = {Map, NamedSection, Flag};
const view = {extend: value => value};
const settings = new Function('form', 'view', source)(form, view);
const rendered = settings.render();

assert.equal(rendered.config, 'doorfast-automation');
assert.equal(rendered.option.default, '0');
assert.equal(rendered.option.rmempty, false);
assert.match(rendered.option.description, /默认关闭/);
assert.match(rendered.option.description, /仅在主机模式下生效/);
assert.match(rendered.option.description, /向上召梯/);
assert.doesNotMatch(source, /require rpc/);
assert.doesNotMatch(source, /automation_status/);
