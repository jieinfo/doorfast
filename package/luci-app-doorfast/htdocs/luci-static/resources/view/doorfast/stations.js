'use strict';
'require dom';
'require form';
'require poll';
'require rpc';
'require uci';
'require ui';
'require view';

var callStationCandidates = rpc.declare({
    object: 'doorfast',
    method: 'station_candidates',
    expect: { '': {} }
});

var callStationScan = rpc.declare({
    object: 'doorfast',
    method: 'station_scan',
    expect: { '': {} }
});

function validateStationId(sectionId, value) {
    if (!/^[a-z][a-z0-9_]{0,31}$/.test(value))
        return '门口机 ID 必须以小写字母开头，只能包含小写字母、数字和下划线，且长度不超过 32。';
    return true;
}

function validateLogicalAddress(sectionId, value) {
    var match = /^32:([0-9a-fA-F]{2}):([0-9a-fA-F]{2}):00:([0-9a-fA-F]{2}):00$/.exec(value);
    var parts;

    if (match === null)
        return '门口机逻辑地址必须是 32:bb:uu:00:gg:00 格式。';
    parts = match.slice(1).map(function(part) { return Number.parseInt(part, 16); });
    if (parts.some(function(part) {
        return part === 0 || (part >> 4) > 9 || (part & 0x0f) > 9;
    }))
        return '门口机逻辑地址中的楼栋、单元或门口机号无效。';
    return true;
}

function validateStreamName(sectionId, value) {
    if (!/^[A-Za-z0-9_-]{1,64}$/.test(value))
        return '流名称只能包含 ASCII 字母、数字、下划线或连字符，且长度不超过 64。';
    return true;
}

function errorText(error) {
    return error && typeof error.message === 'string'
        ? error.message : String(error);
}

return view.extend({
    validateStationId: validateStationId,

    load: function() {
        return Promise.all([
            uci.load('doorfast'),
            callStationCandidates().catch(function(error) {
                return {candidates: [], error: errorText(error)};
            })
        ]);
    },

    buildStationMap: function() {
        var map = new form.Map('doorfast', '门口机',
            '门口机 ID 是不可变的 UCI section 名称。修改后使用页面的保存操作写入配置。');
        var section = map.section(form.GridSection, 'station', '已配置门口机');
        var originalHandleAdd = section.handleAdd;
        var option;

        section.anonymous = false;
        section.addremove = true;
        section.addbtntitle = '添加门口机';
        section.sectiontitle = function(sectionId) { return sectionId; };
        section.handleAdd = function(event, name) {
            var validation = validateStationId('row', name);

            if (validation !== true) {
                ui.addNotification('门口机 ID 无效', E('p', {}, [validation]),
                    'warning');
                return null;
            }
            return originalHandleAdd.call(this, event, name);
        };

        option = section.option(form.Flag, 'enabled', '启用');
        option.default = option.enabled;
        option.rmempty = false;

        option = section.option(form.Value, 'name', '名称');
        option.rmempty = false;

        option = section.option(form.Value, 'logical_address', '逻辑地址');
        option.placeholder = '32:02:01:00:02:00';
        option.rmempty = false;
        option.validate = validateLogicalAddress;

        option = section.option(form.Value, 'ipv4', 'IPv4 地址');
        option.datatype = 'ip4addr';
        option.rmempty = true;

        option = section.option(form.ListValue, 'route_preference', '路由偏好');
        option.value('discover_first', '优先发现');
        option.value('fixed', '固定地址');
        option.default = 'discover_first';
        option.rmempty = false;

        option = section.option(form.Value, 'stream_name', '流名称');
        option.placeholder = 'doorfast_gate_main';
        option.rmempty = false;
        option.validate = validateStreamName;

        this.stationMap = map;
        this.stationSection = section;
        return map;
    },

    renderCandidates: function(payload) {
        var self = this;
        var candidates = payload && Array.isArray(payload.candidates)
            ? payload.candidates : [];

        if (payload && payload.error) {
            return E('div', { 'class': 'alert-message warning' }, [
                '无法读取发现结果：' + payload.error
            ]);
        }
        if (candidates.length === 0)
            return E('p', {}, ['尚未发现门口机。']);
        return E('div', { 'class': 'table' }, [
            E('div', { 'class': 'tr table-titles' }, [
                E('div', { 'class': 'th' }, ['逻辑地址']),
                E('div', { 'class': 'th' }, ['IPv4 地址']),
                E('div', { 'class': 'th' }, ['回复数']),
                E('div', { 'class': 'th cbi-section-actions' }, ['操作'])
            ])
        ].concat(candidates.map(function(candidate) {
            return E('div', { 'class': 'tr' }, [
                E('div', { 'class': 'td' }, [candidate.logical_address]),
                E('div', { 'class': 'td' }, [candidate.ipv4]),
                E('div', { 'class': 'td' }, [String(candidate.reply_count)]),
                E('div', { 'class': 'td cbi-section-actions' }, [
                    E('button', {
                        'class': 'btn cbi-button cbi-button-add',
                        'disabled': candidate.configured || null,
                        'click': function() {
                            var stationId = window.prompt(
                                '请输入新门口机 ID（小写字母、数字和下划线）');

                            if (stationId === null)
                                return null;
                            return self.adoptCandidate(candidate, stationId);
                        }
                    }, [candidate.configured ? '已配置' : '采用'])
                ])
            ]);
        })));
    },

    refreshCandidates: function() {
        var self = this;

        return callStationCandidates().then(function(payload) {
            dom.content(self.candidateNode, self.renderCandidates(payload));
        }).catch(function(error) {
            dom.content(self.candidateNode, self.renderCandidates({
                candidates: [], error: errorText(error)
            }));
        });
    },

    handleScan: function() {
        var self = this;

        return callStationScan().then(function() {
            return self.refreshCandidates();
        }).catch(function(error) {
            ui.addNotification('无法启动门口机扫描', E('p', {}, [
                '后端拒绝扫描：' + errorText(error)
            ]), 'danger');
        });
    },

    adoptCandidate: function(candidate, stationId) {
        var validation = validateStationId('row', stationId);
        var sectionId;
        var values;

        if (validation !== true) {
            ui.addNotification('门口机 ID 无效', E('p', {}, [validation]),
                'warning');
            return Promise.resolve();
        }
        sectionId = this.stationMap.data.add('doorfast', 'station', stationId);
        values = {
            enabled: '1',
            name: stationId,
            logical_address: candidate.logical_address,
            ipv4: candidate.ipv4,
            route_preference: 'discover_first',
            stream_name: 'doorfast_' + stationId
        };
        Object.keys(values).forEach(function(name) {
            uci.set('doorfast', sectionId, name, values[name]);
        });
        this.stationMap.addedSection = sectionId;
        return this.stationSection.renderMoreOptionsModal(sectionId);
    },

    render: function(data) {
        var self = this;
        var map = this.buildStationMap();
        var candidates = data && data[1] ? data[1] : {candidates: []};

        this.candidateNode = E('div', {}, [this.renderCandidates(candidates)]);
        poll.add(function() { return self.refreshCandidates(); }, 5);
        return map.render().then(function(mapNode) {
            return E('div', {}, [
                mapNode,
                E('div', { 'class': 'cbi-section' }, [
                    E('h3', {}, ['发现的门口机']),
                    E('button', {
                        'class': 'btn cbi-button cbi-button-action',
                        'click': function() { return self.handleScan(); }
                    }, ['扫描']),
                    self.candidateNode
                ])
            ]);
        });
    }
});
