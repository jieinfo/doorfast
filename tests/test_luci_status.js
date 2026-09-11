'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');

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

const modelPath = path.join(__dirname, '..', 'package',
    'luci-app-doorfast', 'htdocs', 'luci-static', 'resources',
    'doorfast', 'status_model.js');
const source = fs.readFileSync(modelPath, 'utf8');
function fakeBaseclass() {}
fakeBaseclass.extend = function(properties) {
    function LuCIClass() {}
    Object.assign(LuCIClass.prototype, properties);
    return LuCIClass;
};
const luciFactory = new Function('window', 'document', 'L', 'baseclass',
    'module', source);
const LuCIModel = luciFactory({}, {}, {}, fakeBaseclass, undefined);
assert.equal(typeof LuCIModel, 'function');
assert.equal(typeof new LuCIModel().formatStatus, 'function');
const LuCIModelWithCommonJsGlobal = luciFactory({}, {}, {}, fakeBaseclass,
    { exports: {} });
assert.equal(typeof LuCIModelWithCommonJsGlobal, 'function');

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
    },
    call: {
        session: 'ringing',
        generation: 9,
        command: 'answer',
        dispatch: 'sent',
        confirmation: 'waiting',
        attempts: 1
    }
};

const handshakePayload = JSON.parse(JSON.stringify(payload));
Object.assign(handshakePayload.call, {handshake_mode: 'simulated',
    handshake_active: true, handshake_missed: 2, handshake_next_ms: 1800,
    handshake_dispatch: 'retry', handshake_dropped: 3});
assert.deepEqual(model.formatStatus(handshakePayload).at(-1), {
    title: '保活模拟（仅内存发送）', rows: [
        ['已启动', '是'], ['未回复次数', '2'],
        ['距下次探测（毫秒，最近采样）', '1800'],
        ['发送事务', '等待重试'], ['累计丢弃动作', '3']
    ]});
handshakePayload.call.handshake_missed = -1;
assert.throws(() => model.formatStatus(handshakePayload), TypeError);

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
        title: '通话控制',
        rows: [
            ['会话', '来电振铃'],
            ['会话代次', '9'],
            ['命令', '接听'],
            ['模拟发送', '已发送'],
            ['业务确认', '等待确认'],
            ['发送次数', '1']
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
assert.equal(model.callSessionLabel('talking'), '通话中');
assert.equal(model.callDispatchLabel('failed'), '发送失败');
assert.equal(model.callConfirmationLabel('expired'), '确认超时');
assert.equal(model.unavailableLabel, 'Doorfast 服务未运行或状态接口不可用');
assert.equal(model.staleLabel, '陈旧');
assert.throws(() => model.formatStatus({running: true, mode: 'passive'}));
assert.equal(JSON.stringify(model.formatStatus(payload)).includes('secretkey'),
             false);
assert.equal(JSON.stringify(model.formatStatus(payload)).includes('synthetic'),
             false);
const deployment = {schema_version: 1, configured: true, preflight_safe: false,
    passive_only: true, upstream: {name: 'up', present: true, carrier: true},
    downstream: {name: 'down', present: true},
    management: {name: 'mgmt', present: true, carrier: false},
    recorder: {present: true, state: 'space_guard'}};
assert.deepEqual(model.formatStatus({...payload, deployment}).at(-1), {
    title: '串联部署', rows: [
        ['部署配置', '已启用'], ['部署预检', '未通过'], ['被动模式', '是'],
        ['门禁上联', 'up：已连接'], ['室内机下联', 'down：链路未知'],
        ['管理接口', 'mgmt：未连接'], ['证据记录器', '磁盘余量保护']
    ]
});
deployment.recorder.state = 'stale';
assert.equal(model.formatStatus({...payload, deployment}).at(-1).rows.at(-1)[1], '陈旧');
