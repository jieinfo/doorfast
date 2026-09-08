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

function unsignedText(value, name) {
    if (!Number.isInteger(value) || value < 0)
        throw new TypeError(name + ' must be an unsigned integer');
    return String(value);
}

function formatStatus(payload) {
    var root = requireObject(payload, 'payload');
    var sync = requireObject(root.sync, 'sync');

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
    unavailableLabel: 'Doorfast 服务未运行或状态接口不可用',
    staleLabel: '陈旧'
};

if (typeof module === 'object' && module.exports)
    module.exports = statusModel;

return statusModel;
