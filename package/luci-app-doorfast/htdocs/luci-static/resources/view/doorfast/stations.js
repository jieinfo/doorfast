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

function validateStationName(sectionId, value) {
    if (!/^[\x20-\x7e]{1,64}$/.test(value))
        return '名称必须是 1 到 64 个可打印 ASCII 字符。';
    return true;
}

function validateStreamName(sectionId, value) {
    if (!/^[A-Za-z0-9_-]{1,64}$/.test(value))
        return '流名称只能包含 ASCII 字母、数字、下划线或连字符，且长度不超过 64。';
    return true;
}

function validateStationIpv4(value, fixed) {
    var parts;

    if (value === '')
        return fixed ? '固定路由必须填写 IPv4 地址。' : true;
    parts = value.split('.');
    if (parts.length !== 4 || parts.some(function(part) {
        return !/^(?:0|[1-9][0-9]{0,2})$/.test(part) || Number(part) > 255;
    }) || Number(parts[0]) === 0 || Number(parts[0]) === 127 ||
        Number(parts[0]) >= 224)
        return '必须填写可用的单播 IPv4 地址。';
    return true;
}

function optionValue(map, option, sectionId, name) {
    var value = option.formvalue(sectionId);

    if (typeof value !== 'string')
        value = map.data.get(map.config, sectionId, name);
    return typeof value === 'string' ? value : '';
}

function validateUniqueValue(map, option, sectionId, name, value,
    normalize, label) {
    var expected = normalize(value);
    var duplicate = map.data.sections(map.config, 'station').some(function(row) {
        var otherId = row['.name'];
        var otherValue;

        if (otherId === sectionId)
            return false;
        otherValue = optionValue(map, option, otherId, name);
        return otherValue !== '' && normalize(otherValue) === expected;
    });

    return duplicate ? label + '不能重复。' : true;
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
        var logicalAddressOption;
        var ipv4Option;
        var routePreferenceOption;
        var streamNameOption;

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
            if (map.data.get(map.config, name) !== null) {
                ui.addNotification('门口机 ID 已存在', E('p', {}, [
                    '该门口机 ID 已存在，请使用新的 ID。'
                ]), 'warning');
                return null;
            }
            return originalHandleAdd.call(this, event, name);
        };

        option = section.option(form.Flag, 'enabled', '启用');
        option.default = option.enabled;
        option.rmempty = false;

        option = section.option(form.Value, 'name', '名称');
        option.rmempty = false;
        option.validate = validateStationName;

        logicalAddressOption = section.option(
            form.Value, 'logical_address', '逻辑地址');
        logicalAddressOption.placeholder = '32:02:01:00:02:00';
        logicalAddressOption.rmempty = false;
        logicalAddressOption.validate = function(sectionId, value) {
            var validation = validateLogicalAddress(sectionId, value);

            if (validation !== true)
                return validation;
            return validateUniqueValue(map, logicalAddressOption, sectionId,
                'logical_address', value, function(current) {
                    return current.toLowerCase();
                }, '逻辑地址');
        };

        ipv4Option = section.option(form.Value, 'ipv4', 'IPv4 地址');
        ipv4Option.datatype = 'ip4addr';
        ipv4Option.rmempty = true;

        routePreferenceOption = section.option(
            form.ListValue, 'route_preference', '路由偏好');
        routePreferenceOption.value('discover_first', '优先发现');
        routePreferenceOption.value('fixed', '固定地址');
        routePreferenceOption.default = 'discover_first';
        routePreferenceOption.rmempty = false;
        ipv4Option.validate = function(sectionId, value) {
            return validateStationIpv4(value,
                optionValue(map, routePreferenceOption, sectionId,
                    'route_preference') === 'fixed');
        };

        streamNameOption = section.option(form.Value, 'stream_name', '流名称');
        streamNameOption.placeholder = 'doorfast_gate_main';
        streamNameOption.rmempty = false;
        streamNameOption.validate = function(sectionId, value) {
            var validation = validateStreamName(sectionId, value);

            if (validation !== true)
                return validation;
            return validateUniqueValue(map, streamNameOption, sectionId,
                'stream_name', value, function(current) { return current; },
                '流名称');
        };

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
        if (this.stationMap.data.get('doorfast', stationId) !== null) {
            ui.addNotification('门口机 ID 已存在', E('p', {}, [
                '该门口机 ID 已存在，请使用新的 ID。'
            ]), 'warning');
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
            this.stationMap.data.set('doorfast', sectionId, name, values[name]);
        }, this);
        this.stationMap.addedSection = sectionId;
        return this.stationSection.renderMoreOptionsModal(sectionId);
    },

    render: function(data) {
        var self = this;
        var map = this.buildStationMap();
        var candidates = data && data[1] ? data[1] : {candidates: []};

        this.candidateNode = E('div', {}, [this.renderCandidates(candidates)]);
        if (this.candidatePoll === undefined) {
            this.candidatePoll = function() { return self.refreshCandidates(); };
            poll.add(this.candidatePoll, 5);
        }
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
