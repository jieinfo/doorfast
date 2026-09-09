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

return view.extend({
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
                content = statusModel.formatStatus(payload).map(renderSection);
                if (stale) {
                    content.unshift(E('div', {
                        'class': 'alert-message warning'
                    }, [statusModel.staleLabel]));
                }
            } catch (error) {
                content = [
                    E('div', { 'class': 'alert-message warning' }, [
                        statusModel.unavailableLabel
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
        poll.add(function() { return self.refreshStatus(); }, 5);
        return E('div', {}, [
            E('h2', {}, ['Doorfast']),
            E('p', {}, ['仅显示被动观察与同步维护状态。']),
            this.statusNode
        ]);
    },

    handleSaveApply: null,
    handleSave: null,
    handleReset: null
});
