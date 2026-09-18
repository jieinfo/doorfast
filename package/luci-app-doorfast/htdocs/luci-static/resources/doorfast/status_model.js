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

function formatMediaStatus(payload) {
    var media = requireObject(payload, 'media');
    var state = enumLabel(media.state, {
        unavailable: 'Unavailable',
        idle: 'Idle',
        requesting: 'Requesting',
        awaiting_video: 'Awaiting video',
        publishing: 'Publishing',
        viewing: 'Viewing',
        stopping: 'Stopping',
        failed: 'Failed'
    }, 'media state');
    var failure = media.failure;

    if (typeof media.available !== 'boolean' ||
        typeof media.encoder_running !== 'boolean' ||
        typeof media.rtsp_password_set !== 'boolean')
        throw new TypeError('media status flag must be boolean');
    if (failure === undefined)
        failure = '';
    if (typeof failure !== 'string' || !/^[a-z_]*$/.test(failure))
        throw new TypeError('invalid media failure');

    return [
        ['State', state],
        ['Generation', unsignedText(media.generation, 'media generation')],
        ['Effective capacity', unsignedText(media.effective_capacity,
                                              'effective capacity')],
        ['Queue drops', unsignedText(media.queue_drops, 'queue drops')],
        ['RTSP password', media.rtsp_password_set ? 'Set' : 'Not set'],
        ['Encoder', media.encoder_running ? 'Running' : 'Stopped'],
        ['Failure', failure === '' ? 'None' : failure]
    ];
}

function localizedMediaStatus(payload) {
    var labels = {
        State: '状态', Generation: '代次', 'Effective capacity': '有效容量',
        'Queue drops': '队列丢帧', 'RTSP password': 'RTSP 密码',
        Encoder: '编码器', Failure: '失败原因'
    };
    var states = {
        Unavailable: '模块不可用', Idle: '空闲', Requesting: '请求中',
        'Awaiting video': '等待视频', Publishing: '发布中', Viewing: '观看中',
        Stopping: '停止中', Failed: '失败', Set: '已设置', 'Not set': '未设置',
        Running: '运行中', Stopped: '已停止', None: '无'
    };

    return formatMediaStatus(payload).map(function(row) {
        return [labels[row[0]], Object.prototype.hasOwnProperty.call(
            states, row[1]) ? states[row[1]] : row[1]];
    });
}

function formatStatus(payload) {
    var root = requireObject(payload, 'payload');
    var sync = requireObject(root.sync, 'sync');
    var call = requireObject(root.call, 'call');

    if (typeof root.running !== 'boolean' ||
        (root.mode !== 'passive' && root.mode !== 'active_host'))
        throw new TypeError('invalid service status');

    var sections = [
        {
            title: '服务',
            rows: [
                ['运行状态', root.running ? '运行中' : '已停止'],
                ['模式', root.mode === 'active_host' ? '主机模式' : '被动观察']
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
                [root.mode === 'active_host' ? '发送状态' : '模拟发送',
                    callDispatchLabel(call.dispatch)],
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
    if (root.elevator) {
        var elevator = requireObject(root.elevator, 'elevator');
        var direction = enumLabel(elevator.direction,
            {up: '上行', down: '下行'}, 'elevator direction');
        var elevatorRows = [
            ['事务状态', enumLabel(elevator.state, {
                idle: '空闲', waiting: '等待回包',
                protocol_completed: '协议完成', expired: '已超时',
                send_failed: '发送失败', cancelled: '已取消'
            }, 'elevator state')],
            ['方向', direction],
            ['事务编号', unsignedText(elevator.transaction_id, 'transaction_id')],
            ['发送次数', unsignedText(elevator.attempts, 'attempts')],
            ['成功发送', unsignedText(elevator.successful_sends, 'successful_sends')],
            ['实体动作已确认', yesNo(elevator.physical_result_confirmed)],
            ['状态数据', yesNo(elevator.status_valid)],
            ['状态年龄（毫秒）', unsignedText(elevator.status_age_ms, 'status_age_ms')]
        ];
        if (elevator.status_valid) {
            if (!Array.isArray(elevator.entries) || elevator.entries.length > 8)
                throw new TypeError('invalid elevator entries');
            elevatorRows.push(['电梯数量', unsignedText(elevator.entries.length,
                'elevator entries')]);
        }
        sections.push({title: '电梯控制', rows: elevatorRows});
    }
    if (call.handshake_mode === 'simulated' || call.handshake_mode === 'udp') {
        sections.push({title: call.handshake_mode === 'udp'
            ? '通话保活（UDP）' : '保活模拟（仅内存发送）', rows: [
            ['已启动', yesNo(call.handshake_active)],
            ['未回复次数', unsignedText(call.handshake_missed, 'handshake_missed')],
            ['距下次探测（毫秒，最近采样）', unsignedText(call.handshake_next_ms, 'handshake_next_ms')],
            ['发送事务', callDispatchLabel(call.handshake_dispatch)],
            ['累计丢弃动作', unsignedText(call.handshake_dropped, 'handshake_dropped')]
        ]});
    }
    if (root.media && root.media.installed === true) {
        sections.push({title: '媒体预览', rows: localizedMediaStatus(
            requireObject(root.media, 'media'))});
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
    formatMediaStatus: formatMediaStatus,
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
