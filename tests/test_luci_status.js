'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');

const model = require(path.join(
    __dirname,
    '..',
    'package',
    'luci-app-doorfast',
    'htdocs',
    'luci-static',
    'resources',
    'doorfast',
    'status_model.js'
));

const payload = {
    running: true,
    mode: 'passive',
    sync: {
        phase: 'periodic',
        role: 'follower',
        version: 23,
        periodic_misses: 1,
        online_peers: 2,
        registered_adapters: 2,
        enabled_adapters: 0,
        last_opcode: 3,
        last_handled: true,
        last_accepted: true,
        last_rejected: false,
        resend_local: false
    }
};

assert.deepEqual(model.formatStatus(payload), [
    {
        title: '服务',
        rows: [
            ['运行状态', '运行中'],
            ['模式', '被动观察']
        ]
    },
    {
        title: '在线同步',
        rows: [
            ['阶段', '周期维护'],
            ['角色', '同步跟随者'],
            ['版本', '23'],
            ['周期丢失', '1'],
            ['在线候选', '2'],
            ['适配器', '0 / 2']
        ]
    },
    {
        title: '最近同步报文',
        rows: [
            ['操作码', '3'],
            ['已处理', '是'],
            ['已接受', '是'],
            ['已拒绝', '否'],
            ['需要重发', '否']
        ]
    }
]);

assert.equal(model.roleLabel('maintainer'), '同步维护者');
assert.equal(model.roleLabel('follower'), '同步跟随者');
assert.equal(model.roleLabel('starting'), '选举中');
assert.equal(model.roleLabel('down'), '离线');
assert.equal(model.unavailableLabel, 'Doorfast 服务未运行或状态接口不可用');
assert.equal(model.staleLabel, '陈旧');
assert.throws(() => model.formatStatus({running: true, mode: 'passive'}));
assert.equal(JSON.stringify(model.formatStatus(payload)).includes('secretkey'),
             false);
assert.equal(JSON.stringify(model.formatStatus(payload)).includes('synthetic'),
             false);
