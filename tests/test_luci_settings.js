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
                if (optionName === 'call_elev') {
                    assert.equal(label, '来电自动向上召梯');
                    this.callElev = {disabled: '0'};
                    return this.callElev;
                }
                assert.equal(optionName, 'auto_unlock');
                assert.equal(label, '来电自动解锁');
                this.autoUnlock = {disabled: '0'};
                return this.autoUnlock;
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
assert.equal(rendered.callElev.default, '0');
assert.equal(rendered.callElev.rmempty, false);
assert.equal(rendered.autoUnlock.default, '0');
assert.equal(rendered.autoUnlock.rmempty, false);
assert.match(source, /默认关闭/);
assert.match(source, /仅在主机模式下生效/);
assert.match(source, /向上召梯/);
assert.match(source, /来电自动解锁/);
assert.doesNotMatch(source, /require rpc/);
assert.doesNotMatch(source, /automation_status/);
