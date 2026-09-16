'use strict';
'require dom';
'require poll';
'require rpc';
'require view';

var callLogs = rpc.declare({
    object: 'doorfast',
    method: 'logs',
    expect: { '': {} }
});

function formatTimestamp(timestamp) {
    return timestamp === undefined ? '未知' : String(timestamp) + ' ms（单调时钟）';
}

function renderEntries(payload) {
    var entries = Array.isArray(payload && payload.entries) ?
        payload.entries.slice(-200) : [];

    if (!entries.length) {
        return E('div', { 'class': 'alert-message info' }, [
            '尚未记录运行事件。服务启动或收到通话事件后会显示在这里。'
        ]);
    }
    return E('div', { 'class': 'table' }, entries.map(function(entry) {
        return E('div', { 'class': 'tr' }, [
            E('div', { 'class': 'td left', 'width': '12%' }, [
                String(entry.sequence)
            ]),
            E('div', { 'class': 'td left', 'width': '26%' }, [
                formatTimestamp(entry.timestamp_ms)
            ]),
            E('div', { 'class': 'td left' }, [String(entry.message || '')])
        ]);
    }));
}

return view.extend({
    load: function() {
        return callLogs().catch(function() { return null; });
    },

    refresh: function() {
        var self = this;
        return callLogs().then(function(payload) {
            self.lastPayload = payload;
            dom.content(self.contentNode, renderEntries(payload));
        }).catch(function() {
            dom.content(self.contentNode, E('div', {
                'class': 'alert-message warning'
            }, ['Doorfast 日志接口不可用；请确认服务正在运行。']));
        });
    },

    render: function(payload) {
        var self = this;
        this.lastPayload = payload;
        this.contentNode = E('div', {}, [renderEntries(payload)]);
        poll.add(function() { return self.refresh(); }, 5);
        return E('div', {}, [
            E('h2', {}, ['Doorfast 日志']),
            E('p', {}, ['只读显示最近 200 条 Doorfast 运行事件。日志保存在内存中，服务重启后会清空；时间为设备启动后的单调毫秒。']),
            E('div', { 'class': 'table' }, [
                E('div', { 'class': 'tr table-titles' }, [
                    E('div', { 'class': 'th left', 'width': '12%' }, ['序号']),
                    E('div', { 'class': 'th left', 'width': '26%' }, ['时间']),
                    E('div', { 'class': 'th left' }, ['事件'])
                ])
            ]),
            this.contentNode
        ]);
    },

    handleSaveApply: null,
    handleSave: null,
    handleReset: null
});
