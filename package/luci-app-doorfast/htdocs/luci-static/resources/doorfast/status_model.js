'use strict';

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

    return [
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

if (typeof module === 'object' && module.exports)
    module.exports = statusModel;

return statusModel;
