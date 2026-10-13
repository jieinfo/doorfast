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
        ['发送事务', 'Waiting to retry'], ['累计丢弃动作', '3']
    ]});
const udpHandshakePayload = JSON.parse(JSON.stringify(handshakePayload));
udpHandshakePayload.mode = 'active_host';
udpHandshakePayload.call.handshake_mode = 'udp';
assert.equal(model.formatStatus(udpHandshakePayload)[2].rows[3][0], '发送状态');
assert.equal(model.formatStatus(udpHandshakePayload).at(-1).title,
    '通话保活（UDP）');
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
            ['发送次数', '1'],
            ['计时器', '未知'], ['剩余时间（最近采样）', '未知'],
            ['结束原因', '未知'], ['故障分类', '未知'],
            ['最近结果门口机', '未知'], ['最近结果代次', '未知']
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
const outgoingPayload = JSON.parse(JSON.stringify(payload));
outgoingPayload.mode = 'active_host';
outgoingPayload.call.command = 'call';
assert.equal(model.formatStatus(outgoingPayload)[0].rows[0][1], '运行中');
assert.equal(model.formatStatus(outgoingPayload)[2].rows[2][1], '呼叫室外机');
assert.equal(model.unavailableLabel, 'Doorfast 服务未运行或状态接口不可用');
assert.equal(model.staleLabel, '陈旧');
const elevatorPayload = JSON.parse(JSON.stringify(payload));
elevatorPayload.mode = 'active_host';
elevatorPayload.elevator = {
    state: 'protocol_completed', direction: 'up', transaction_id: 3,
    attempts: 2, successful_sends: 2, physical_result_confirmed: false,
    status_valid: true, status_age_ms: 12, entries: [{floor: 1}]
};
assert.equal(model.formatStatus(elevatorPayload)[0].rows[1][1], '主机模式');
assert.deepEqual(model.formatStatus(elevatorPayload).at(-1), {
    title: '电梯控制', rows: [
        ['事务状态', '协议完成'], ['方向', '上行'], ['事务编号', '3'],
        ['发送次数', '2'], ['成功发送', '2'], ['实体动作已确认', '否'],
        ['状态数据', '是'], ['状态年龄（毫秒）', '12'], ['电梯数量', '1']
    ]
});
assert.throws(() => model.formatStatus({running: true, mode: 'passive'}));
assert.equal(JSON.stringify(model.formatStatus(payload)).includes('secretkey'),
             false);
assert.equal(JSON.stringify(model.formatStatus(payload)).includes('synthetic'),
             false);
const aggregateMediaPayload = JSON.parse(JSON.stringify(payload));
aggregateMediaPayload.media = {
    installed: true,
    available: true,
    status_revision: 1,
    configured_capacity: 3,
    effective_capacity: 3,
    active_encoders: 0,
    rtsp_password_set: true,
    sessions: []
};
assert.doesNotThrow(() => model.formatStatus(aggregateMediaPayload));
assert.deepEqual(model.formatStatus(aggregateMediaPayload).at(-1), {
    title: '媒体预览', rows: [
        ['状态', '空闲'], ['代次', '1'], ['有效容量', '3'], ['队列丢帧', '0'],
        ['RTSP 密码', '已设置'], ['编码器', '已停止'], ['失败原因', '无']
    ]
});
aggregateMediaPayload.media.sessions = [{
    station_id: 'doorfast_1_unit', control_requests: 3, busy_replies: 2,
    confirmed_ms: 1300, first_frame_ms: 1700, publication_ms: 1700
}];
assert.equal(model.formatStatus(aggregateMediaPayload).at(-1).rows.at(-1)[0],
             '启动：doorfast_1_unit');
assert.match(model.formatMediaStatus(aggregateMediaPayload.media).at(-1)[1],
             /requests=3, busy=2/);
const phaseSession = aggregateMediaPayload.media.sessions[0];
const malformedStartupPayload = structuredClone(aggregateMediaPayload);
malformedStartupPayload.media.sessions[0].control_requests = -1;
assert.doesNotThrow(() => model.formatStatus(malformedStartupPayload));
const diagnosticPayload = structuredClone(payload);
Object.assign(diagnosticPayload.call, {timer: 'confirmation', remaining_ms: 500,
    end_reason: 'none', fault: 'confirmation_timeout', last_station_id: 'gate_main',
    last_generation: 8});
diagnosticPayload.media = {...aggregateMediaPayload.media, sessions: [{
    station_id: 'gate_main', generation: 9, state: 'awaiting_video',
    timer: 'first_frame', remaining_ms: 900, end_reason: 'none', failure_reason: 'none'
}], last_results: [{station_id: 'gate_main', generation: 8, timer: 'none',
    remaining_ms: 0, end_reason: 'local_hangup', failure_reason: 'cancelled'}]};
const diagnosticSections = model.formatStatus(diagnosticPayload);
assert.ok(diagnosticSections[2].rows.some(row => row[0] === '计时器' && row[1] === '确认'));
assert.ok(diagnosticSections[2].rows.some(row => row[1] === '500 ms'));
assert.ok(diagnosticSections.at(-1).rows.some(row => row[0] === '当前媒体：gate_main' &&
    /代次=9.*等待首帧.*900 ms.*结束=无.*故障=无/.test(row[1])));
assert.ok(diagnosticSections.at(-1).rows.some(row => row[0] === '最近媒体结果：gate_main' &&
    /代次=8.*主动挂断.*已取消/.test(row[1])));
for (const bad of [undefined, null, -1, '900', Number.MAX_SAFE_INTEGER + 1]) {
    diagnosticPayload.call.remaining_ms = bad;
    diagnosticPayload.call.timer = 'confirmation';
    diagnosticPayload.call.fault = {password: 'hidden'};
    diagnosticPayload.media.sessions[0].remaining_ms = bad;
    diagnosticPayload.media.sessions[0].timer = 'first_frame';
    diagnosticPayload.media.sessions.push(null);
    diagnosticPayload.media.last_results.push({station_id: 'bad', generation: -1});
    const formatted = model.formatStatus(diagnosticPayload);
    assert.equal(formatted[0].title, '服务');
    assert.ok(formatted[2].rows.some(row => row[0] === '剩余时间（最近采样）' && row[1] === '未知'));
    assert.ok(formatted.at(-1).rows.some(row => /剩余=未知/.test(row[1])));
    assert.doesNotMatch(JSON.stringify(formatted), /hidden|future_timer/);
}
diagnosticPayload.call.timer = 'future_timer';
diagnosticPayload.call.remaining_ms = 900;
diagnosticPayload.media.sessions[0].timer = 'future_timer';
diagnosticPayload.media.sessions[0].remaining_ms = 900;
const unknownDiagnostics = model.formatStatus(diagnosticPayload);
assert.ok(unknownDiagnostics[2].rows.some(row => row[0] === '计时器' && row[1] === '未知'));
assert.ok(unknownDiagnostics.at(-1).rows.some(row => /剩余=未知/.test(row[1])));
delete diagnosticPayload.media.sessions[0].timer;
delete diagnosticPayload.media.sessions[0].remaining_ms;
assert.ok(model.formatStatus(diagnosticPayload).at(-1).rows.some(row => /剩余=未知/.test(row[1])));
diagnosticPayload.call.timer = 'first_frame';
diagnosticPayload.call.remaining_ms = 100;
diagnosticPayload.call.fault = 'encoder_failed';
diagnosticPayload.media.sessions[0].end_reason = 'heartbeat_timeout';
diagnosticPayload.media.sessions[0].failure_reason = 'heartbeat_timeout';
const wrongDomainDiagnostics = model.formatStatus(diagnosticPayload);
assert.ok(wrongDomainDiagnostics[2].rows.some(row => row[0] === '计时器' && row[1] === '未知'));
assert.ok(wrongDomainDiagnostics[2].rows.some(row => row[0] === '故障分类' && row[1] === '未知'));
assert.ok(wrongDomainDiagnostics.at(-1).rows.some(row => /结束=未知, 故障=未知/.test(row[1])));
phaseSession.started_ms = 1000;
assert.match(model.formatMediaStatus(aggregateMediaPayload.media).at(-1)[1],
             /confirmed=300 ms, first=700 ms, publish=700 ms/);
phaseSession.confirmed_ms = 0;
assert.match(model.formatMediaStatus(aggregateMediaPayload.media).at(-1)[1],
             /confirmed=Not yet/);
phaseSession.first_frame_ms = 900;
assert.match(model.formatMediaStatus(aggregateMediaPayload.media).at(-1)[1],
             /first=Unavailable/);
assert.match(model.formatStatus(aggregateMediaPayload).at(-1).rows.at(-1)[1],
             /confirmed=尚未发生/);
delete phaseSession.started_ms;
assert.match(model.formatMediaStatus(aggregateMediaPayload.media).at(-1)[1],
             /publish=Unavailable/);
phaseSession.started_ms = 0;
phaseSession.first_frame_ms = 500;
assert.match(model.formatMediaStatus(aggregateMediaPayload.media).at(-1)[1],
             /first=500 ms/);
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

const statusViewPath = path.join(__dirname, '..', 'package',
    'luci-app-doorfast', 'htdocs', 'luci-static', 'resources', 'view',
    'doorfast', 'status.js');
const statusViewSource = fs.readFileSync(statusViewPath, 'utf8');
assert.doesNotMatch(statusViewSource, /form\.Map/);
assert.doesNotMatch(statusViewSource, /media_enabled|media_credentials/);

const statusPollQueue = [];
const statusPoll = {
    add: callback => {
        if (statusPollQueue.includes(callback))
            return false;
        statusPollQueue.push(callback);
        return true;
    }
};
const statusRpc = {declare: () => () => Promise.resolve(payload)};
const statusView = {extend: properties => properties};
const statusDom = {content: () => {}};
const statusElement = () => ({});
const statusViewModel = {
    unavailableLabel: 'unavailable', staleLabel: 'stale',
    formatStatus: () => []
};
const statusPageDefinition = new Function('dom', 'poll', 'rpc', 'view',
    'statusModel', 'E', statusViewSource)(statusDom, statusPoll, statusRpc,
    statusView, statusViewModel, statusElement);
const statusPage = {...statusPageDefinition};
statusPage.render(payload);
statusPage.render(payload);
assert.equal(statusPollQueue.length, 1);

let renderedStatus;
const diagnosticPage = new Function('dom', 'poll', 'rpc', 'view',
    'statusModel', 'E', statusViewSource)(
    {content: (_node, content) => { renderedStatus = content; }},
    statusPoll, statusRpc, statusView, model,
    (tag, attrs, children) => ({tag, attrs, children}));
diagnosticPage.renderCurrent(outgoingPayload, false);
assert.match(JSON.stringify(renderedStatus), /运行中/);
assert.doesNotMatch(JSON.stringify(renderedStatus), /服务未运行/);
diagnosticPage.renderCurrent({running: true}, false);
assert.match(JSON.stringify(renderedStatus), /状态数据解析失败/);
assert.doesNotMatch(JSON.stringify(renderedStatus), /服务未运行/);
diagnosticPage.renderCurrent(null, false);
assert.match(JSON.stringify(renderedStatus), /状态接口不可用/);
