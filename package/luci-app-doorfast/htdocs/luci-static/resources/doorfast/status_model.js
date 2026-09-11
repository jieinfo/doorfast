'use strict';
'require baseclass';

function requireObject(value, name) {
    if (value === null || typeof value !== 'object' || Array.isArray(value))
        throw new TypeError(name + ' must be an object');
    return value;
}

function roleLabel(role) {
    var labels = {
        down: '离线',
        starting: '选举中',
        maintainer: '同步维护者',
        follower: '同步跟随者'
    };

    if (!Object.prototype.hasOwnProperty.call(labels, role))
        throw new TypeError('invalid sync role');
    return labels[role];
}

function phaseLabel(phase) {
    var labels = {
        down: '离线',
        wait_sync: '等待同步',
        sync_ask: '同步询问',
        sync_choose: '维护者选举',
        periodic: '周期维护'
    };

    if (!Object.prototype.hasOwnProperty.call(labels, phase))
        throw new TypeError('invalid sync phase');
    return labels[phase];
}

function yesNo(value) {
    if (typeof value !== 'boolean')
        throw new TypeError('status flag must be boolean');
    return value ? '是' : '否';
}

function enumLabel(value, labels, name) {
    if (typeof value !== 'string' ||
        !Object.prototype.hasOwnProperty.call(labels, value))
        throw new TypeError('invalid ' + name);
    return labels[value];
}

function callSessionLabel(state) {
    return enumLabel(state, {
        idle: '空闲',
        preview: '预览',
        ringing: '来电振铃',
        talking: '通话中',
        ended: '已结束'
    }, 'call session');
}

function callCommandLabel(command) {
    return enumLabel(command, {
        none: '无',
        answer: '接听',
        hangup: '挂断'
    }, 'call command');
}

function callDispatchLabel(state) {
    return enumLabel(state, {
        empty: '空闲',
        queued: '已入队',
        sending: '发送中',
        retry: '等待重试',
        sent: '已发送',
        failed: '发送失败',
        timeout: '发送超时',
        cancelled: '已取消'
    }, 'call dispatch');
}

function callConfirmationLabel(state) {
    return enumLabel(state, {
        empty: '无',
        waiting: '等待确认',
        confirmed: '已确认',
        expired: '确认超时',
        cancelled: '已取消'
    }, 'call confirmation');
}

function unsignedText(value, name) {
    if (!Number.isInteger(value) || value < 0)
        throw new TypeError(name + ' must be an unsigned integer');
    return String(value);
}

function formatStatus(payload) {
    var root = requireObject(payload, 'payload');
    var sync = requireObject(root.sync, 'sync');
    var call = requireObject(root.call, 'call');

    if (typeof root.running !== 'boolean' || root.mode !== 'passive')
        throw new TypeError('invalid service status');

    var sections = [
        {
            title: '服务',
            rows: [
                ['运行状态', root.running ? '运行中' : '已停止'],
                ['模式', '被动观察']
            ]
        },
        {
            title: '在线同步',
            rows: [
                ['阶段', phaseLabel(sync.phase)],
                ['角色', roleLabel(sync.role)],
                ['版本', unsignedText(sync.version, 'version')],
                ['周期丢失', unsignedText(sync.periodic_misses,
                                          'periodic_misses')],
                ['在线候选', unsignedText(sync.online_peers,
                                          'online_peers')],
                ['适配器', unsignedText(sync.enabled_adapters,
                                        'enabled_adapters') + ' / ' +
                             unsignedText(sync.registered_adapters,
                                          'registered_adapters')]
            ]
        },
        {
            title: '通话控制',
            rows: [
                ['会话', callSessionLabel(call.session)],
                ['会话代次', unsignedText(call.generation, 'generation')],
                ['命令', callCommandLabel(call.command)],
                ['模拟发送', callDispatchLabel(call.dispatch)],
                ['业务确认', callConfirmationLabel(call.confirmation)],
                ['发送次数', unsignedText(call.attempts, 'attempts')]
            ]
        },
        {
            title: '最近同步报文',
            rows: [
                ['操作码', unsignedText(sync.last_opcode, 'last_opcode')],
                ['已处理', yesNo(sync.last_handled)],
                ['已接受', yesNo(sync.last_accepted)],
                ['已拒绝', yesNo(sync.last_rejected)],
                ['需要重发', yesNo(sync.resend_local)]
            ]
        }
    ];
    if (call.handshake_mode === 'simulated') {
        sections.push({title: '保活模拟（仅内存发送）', rows: [
            ['已启动', yesNo(call.handshake_active)],
            ['未回复次数', unsignedText(call.handshake_missed, 'handshake_missed')],
            ['距下次探测（毫秒，最近采样）', unsignedText(call.handshake_next_ms, 'handshake_next_ms')],
            ['发送事务', callDispatchLabel(call.handshake_dispatch)],
            ['累计丢弃动作', unsignedText(call.handshake_dropped, 'handshake_dropped')]
        ]});
    }
    if (root.deployment && root.deployment.schema_version === 1) {
        var deployment = root.deployment;
        var rows = [['部署配置', deployment.configured === true ? '已启用' : '未启用或不可读取']];
        if (deployment.configured === true) {
            rows.push(['部署预检', deployment.preflight_safe === true ? '通过' : '未通过']);
            rows.push(['被动模式', deployment.passive_only === true ? '是' : '未确认']);
            ['upstream', 'downstream', 'management'].forEach(function(key, index) {
                var link = deployment[key] || {};
                var state = link.present !== true ? '不存在' :
                    typeof link.carrier !== 'boolean' ? '链路未知' : link.carrier ? '已连接' : '未连接';
                rows.push([['门禁上联', '室内机下联', '管理接口'][index],
                    (typeof link.name === 'string' ? link.name : '') + '：' + state]);
            });
            var recorder = deployment.recorder || {};
            var labels = {recording: '记录中', space_guard: '磁盘余量保护',
                io_error: '读写错误', stopped: '已停止', stale: '陈旧'};
            rows.push(['证据记录器', recorder.present === true ?
                (labels[recorder.state] || '未知') : '状态不可用']);
        }
        sections.push({title: '串联部署', rows: rows});
    }
    return sections;
}

var statusModel = {
    formatStatus: formatStatus,
    roleLabel: roleLabel,
    callSessionLabel: callSessionLabel,
    callDispatchLabel: callDispatchLabel,
    callConfirmationLabel: callConfirmationLabel,
    unavailableLabel: 'Doorfast 服务未运行或状态接口不可用',
    staleLabel: '陈旧'
};

/* LuCI's class loader wins if the host page also exposes a CommonJS global. */
if (typeof baseclass !== 'undefined' && baseclass !== null &&
    typeof baseclass.extend === 'function')
    return baseclass.extend(statusModel);

if (typeof module === 'object' && module.exports) {
    module.exports = statusModel;
    return statusModel;
}

return statusModel;
