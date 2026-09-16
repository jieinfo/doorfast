'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'package',
    'luci-app-doorfast', 'htdocs', 'luci-static', 'resources', 'view',
    'doorfast', 'logs.js'), 'utf8');

assert.match(source, /object: 'doorfast'/);
assert.match(source, /method: 'logs'/);
assert.doesNotMatch(source, /logread|exec|form\.Button/);
assert.match(source, /slice\(-200\)/);
assert.match(source, /poll\.add/);
