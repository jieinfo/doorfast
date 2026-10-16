'use strict';
'require dom';
'require poll';
'require rpc';
'require view';
'require doorfast.status_model as statusModel';

var callStatus = rpc.declare({
    object: 'doorfast',
    method: 'status',
    expect: { '': {} }
});

function renderSection(section) {
    return E('div', { 'class': 'cbi-section' }, [
        E('h3', {}, [section.title]),
        E('div', { 'class': 'table' }, section.rows.map(function(row) {
            return E('div', { 'class': 'tr' }, [
                E('div', { 'class': 'td left', 'width': '40%' }, [row[0]]),
                E('div', { 'class': 'td left' }, [row[1]])
            ]);
        }))
    ]);
}

function formatAccessStatus(access) {
    function label(labels, value) {
        return typeof value === 'string' && Object.prototype.hasOwnProperty.call(labels, value)
            ? labels[value] : '未知';
    }
    var unknown = [{title: '门口机开锁', rows: [['状态', '未知（桥接元数据不可用）']]}];
    var phases = { waiting_media: '准备媒体', waiting_reply: '已发送，等待协议回复',
        protocol_completed: '协议完成', protocol_rejected: '协议拒绝',
        preparation_timeout: '准备超时', expired: '回复超时', cancelled: '已取消',
        send_failed: '发送失败' };
    var reasons = {none: '可用', disabled: '已禁用', material_missing: '缺少开锁材料',
        material_invalid: '开锁材料无效', store_unavailable: '凭据存储不可用',
        station_unavailable: '门口机不可用'};
    var modes = {inherit: '继承全局', direct: '独立材料', disabled: '已禁用'};
    if (access === undefined) return [];
    if (!access || access.version !== 1 || !Array.isArray(access.stations) ||
        access.stations.length > 64 || !Array.isArray(access.requests) || access.requests.length > 64)
        return unknown;
    return access.stations.map(function(station) {
        if (!station || typeof station.station_id !== 'string' ||
            !/^[a-z][a-z0-9_]{0,31}$/.test(station.station_id) ||
            typeof station.configured !== 'boolean') return unknown[0];
        var operation = access.requests.find(function(request) {
            return request && request.station_id === station.station_id &&
                typeof request.runtime_id === 'string' && /^[0-9a-f]{16}$/.test(request.runtime_id) &&
                typeof request.request_id === 'string' && /^[0-9a-f]{16}$/.test(request.request_id) &&
                request.request_id !== '0000000000000000';
        });
        function timing(name) {
            var value = operation && operation[name];
            return Number.isSafeInteger(value) && value >= 0 ? String(value) : '未知';
        }
        var metadataKnown = label(modes, station.mode) !== '未知' &&
            label(reasons, station.reason) !== '未知' &&
            ['none', 'global', 'station'].indexOf(station.credential_source) !== -1;
        var rows = [['开锁模式', label(modes, station.mode)],
            ['凭据状态', label(reasons, station.reason)],
            ['有效材料', metadataKnown ? (station.configured ? '可用' : '不可用') : '未知'],
            ['实体动作', '实体动作未确认']];
        if (operation) rows = rows.concat([
            ['最近操作', label(phases, operation.phase)],
            ['准备路径', label({lightweight: '轻量准备', full_media: '兼容媒体', reused: '复用会话'}, operation.preparation_path)],
            ['等待原因', label({none: '无', busy: '忙碌', cooldown: '冷却', waiting_media: '等待媒体',
                compatibility: '兼容路径', station_unavailable: '门口机不可用'}, operation.wait_reason)],
            ['准备耗时（毫秒）', timing('preparation_ms')],
            ['回复耗时（毫秒）', timing('reply_latency_ms')],
            ['总耗时（毫秒）', timing('elapsed_ms')]
        ]);
        return {title: '门口机开锁：' + station.station_id, rows: rows};
    });
}

return view.extend({
    formatAccessStatus: formatAccessStatus,
    load: function() {
        return callStatus().catch(function() { return null; });
    },

    renderCurrent: function(payload, stale) {
        var content;

        if (payload === null) {
            content = [
                E('div', { 'class': 'alert-message warning' }, [
                    statusModel.unavailableLabel
                ])
            ];
        } else {
            try {
                content = statusModel.formatStatus(payload).concat(
                    formatAccessStatus(payload.access)).map(renderSection);
                if (stale) {
                    content.unshift(E('div', {
                        'class': 'alert-message warning'
                    }, [statusModel.staleLabel]));
                }
            } catch (error) {
                content = [
                    E('div', { 'class': 'alert-message warning' }, [
                        statusModel.invalidLabel
                    ])
                ];
            }
        }
        dom.content(this.statusNode, content);
    },

    refreshStatus: function() {
        var self = this;

        return callStatus().then(function(payload) {
            self.lastStatus = payload;
            self.renderCurrent(payload, false);
        }).catch(function() {
            self.renderCurrent(self.lastStatus, self.lastStatus !== null);
        });
    },

    render: function(payload) {
        var self = this;

        this.lastStatus = payload;
        this.statusNode = E('div', {});
        this.renderCurrent(payload, false);
        if (this.statusPoll === undefined) {
            this.statusPoll = function() { return self.refreshStatus(); };
            poll.add(this.statusPoll, 5);
        }
        return E('div', {}, [
            E('h2', {}, ['Doorfast']),
            E('p', {}, ['Doorfast 运行状态。接听、挂断、开锁与召梯由 Home Assistant 提供。']),
            this.statusNode
        ]);
    },

    handleSaveApply: null,
    handleSave: null,
    handleReset: null
});
