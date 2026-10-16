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

var callAccessStatus = rpc.declare({ object: 'doorfast',
    method: 'access_credentials_status', params: ['station_id'], expect: { '': {} } });
var callAccessSet = rpc.declare({ object: 'doorfast',
    method: 'access_credentials_set', params: ['station_id', 'material', 'mode'],
    expect: { '': {} } });
var callAccessClear = rpc.declare({ object: 'doorfast',
    method: 'access_credentials_clear', params: ['station_id'], expect: { '': {} } });

function validUnlockMode(value) {
    return ['inherit', 'direct', 'disabled'].indexOf(value) !== -1;
}

function credentialStatus(payload) {
    if (!payload || typeof payload.configured !== 'boolean' ||
        typeof payload.material_configured !== 'boolean' ||
        !validUnlockMode(payload.mode) ||
        ['none', 'global', 'station'].indexOf(payload.source) === -1)
        return null;
    return { configured: payload.configured, material_configured: payload.material_configured,
        mode: payload.mode, source: payload.source,
        reason: ['none', 'disabled', 'material_missing', 'material_invalid',
            'store_unavailable', 'station_unavailable'].indexOf(payload.reason) !== -1
            ? payload.reason : 'unknown' };
}

function credentialLabel(payload) {
    var status = credentialStatus(payload);
    var labels = { none: '可用', disabled: '已禁用', material_missing: '缺少开锁材料',
        material_invalid: '开锁材料无效', store_unavailable: '凭据存储不可用',
        station_unavailable: '门口机不可用', unknown: '未知原因' };
    return status ? labels[status.reason] + '；独立材料' +
        (status.material_configured ? '已保存' : '未保存') : '运行时状态未知；新门口机请先保存并应用';
}

function applyPendingChanges() {
    return uci.changes().then(function(changes) {
        if (!changes || typeof changes !== 'object' || Array.isArray(changes) ||
            Object.keys(changes).some(function(config) { return !Array.isArray(changes[config]); }))
            throw new Error('pending configuration unavailable');
        if (!Object.keys(changes).some(function(config) { return changes[config].length > 0; }))
            return;
        // LuCI apply() returns void. Only its completion event confirms UCI apply.
        return new Promise(function(resolve, reject) {
            var timer;
            function cleanup() {
                document.removeEventListener('uci-applied', applied);
                window.clearTimeout(timer);
            }
            function applied() { cleanup(); resolve(); }
            document.addEventListener('uci-applied', applied);
            timer = window.setTimeout(function() {
                cleanup();
                reject(new Error('runtime apply not confirmed'));
            }, 120000);
            try { ui.changes.apply(); }
            catch (error) { cleanup(); reject(new Error('runtime apply failed')); }
        });
    });
}

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
    credentialStatus: credentialStatus,

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
            '门口机 ID 是不可变的 UCI section 名称。新门口机请先保存并应用，再填写独立开锁材料。独立材料留空保留；模式以应用后的 UCI 为准。');
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

        this.unlockMode = section.option(form.ListValue, 'unlock_mode', '开锁模式');
        this.unlockMode.value('inherit', '继承全局材料');
        this.unlockMode.value('direct', '独立材料');
        this.unlockMode.value('disabled', '禁用开锁');
        this.unlockMode.default = 'inherit';
        this.unlockMode.rmempty = false;
        this.unlockMode.editable = true;
        this.unlockMode.modalonly = false;
        this.unlockMode.validate = function(sectionId, value) {
            return validUnlockMode(value) ? true : '开锁模式无效。';
        };
        this.accessMaterial = section.option(form.Value, '_access_material', '独立开锁材料');
        this.accessMaterial.password = true;
        this.accessMaterial.editable = true;
        this.accessMaterial.modalonly = false;
        this.accessMaterial.rmempty = true;
        this.accessMaterial.description = '16 位十六进制，留空保留。不会写入 UCI，也不会回显。';
        this.accessMaterial.cfgvalue = function() { return ''; };
        this.accessMaterial.write = this.accessMaterial.remove = function() {};
        this.accessMaterial.validate = function(sectionId, value) {
            return value === '' || /^[0-9a-fA-F]{16}$/.test(value) ? true : '开锁材料必须是 16 位十六进制。';
        };
        this.accessClear = section.option(form.Flag, '_access_clear', '清除独立材料');
        this.accessClear.editable = true;
        this.accessClear.modalonly = false;
        this.accessClear.cfgvalue = function() { return '0'; };
        this.accessClear.write = this.accessClear.remove = function() {};
        this.accessStatus = section.option(form.DummyValue, '_access_status', '当前开锁凭据');
        var self = this;
        this.accessStatus.cfgvalue = function(sectionId) {
            return credentialLabel(self.credentialStatuses && self.credentialStatuses[sectionId]);
        };

        this.stationMap = map;
        this.stationSection = section;
        return map;
    },

    refreshCredentialStatuses: function() {
        var self = this;
        var statuses = {};
        return Promise.all(this.stationMap.data.sections('doorfast', 'station').map(function(row) {
            return callAccessStatus(row['.name']).then(function(payload) {
                statuses[row['.name']] = payload;
            }).catch(function() { statuses[row['.name']] = null; });
        })).then(function() { self.credentialStatuses = statuses; });
    },

    saveStations: function(apply) {
        var self = this;
        var rows;
        var completed = [];
        var failureRow = '';
        var configSaved = false;
        try {
            rows = this.stationMap.data.sections('doorfast', 'station').map(function(row) {
                var id = row['.name'];
                var mode = self.unlockMode.formvalue(id);
                if (typeof mode !== 'string')
                    mode = self.stationMap.data.get('doorfast', id, 'unlock_mode') || 'inherit';
                var material = self.accessMaterial.formvalue(id) || '';
                var clear = self.accessClear.formvalue(id);
                clear = clear === true || clear === '1';
                if (!validUnlockMode(mode) || (material !== '' && !/^[0-9a-fA-F]{16}$/.test(material)) ||
                    (material !== '' && clear))
                    throw new Error('请检查开锁模式、材料格式及清除选项。');
                return { id: id, mode: mode, material: material, clear: clear };
            });
        } catch (error) {
            ui.addNotification('门口机保存失败', E('p', {}, ['请检查开锁模式、材料格式及清除选项。']), 'danger');
            return Promise.reject(new Error('station validation failed'));
        }
        // Validate the whole form and every runtime row before the first private write.
        return this.stationMap.save(function() {
            return self.refreshCredentialStatuses().then(function() {
                rows.forEach(function(row) {
                    var status = credentialStatus(self.credentialStatuses[row.id]);
                    if ((row.material !== '' || row.clear) && (status === null ||
                        ['unknown', 'store_unavailable'].indexOf(status.reason) !== -1)) {
                        failureRow = row.id;
                        throw new Error('save/apply new station first');
                    }
                });
                return rows.reduce(function(previous, row) {
                    return previous.then(function() {
                        if (row.material === '' && !row.clear) return;
                        failureRow = row.id;
                        return (row.clear ? callAccessClear(row.id) :
                            callAccessSet(row.id, row.material, 'direct')).then(function(payload) {
                            var status = credentialStatus(payload);
                            if (status === null || status.material_configured === row.clear ||
                                ['unknown', 'store_unavailable'].indexOf(status.reason) !== -1)
                                throw new Error('credential not confirmed');
                            self.credentialStatuses[row.id] = status;
                            completed.push(row.id);
                            failureRow = '';
                        });
                    });
                }, Promise.resolve());
            });
        }, true).then(function() {
            configSaved = true;
            return apply ? applyPendingChanges().then(function() {
                return self.refreshCredentialStatuses();
            }).then(function() {
                rows.forEach(function(row) {
                    var status = credentialStatus(self.credentialStatuses[row.id]);
                    if (status === null || status.mode !== row.mode || status.reason === 'unknown' ||
                        status.reason === 'store_unavailable' ||
                        (row.mode === 'direct' && status.reason !== 'station_unavailable' &&
                            status.configured !== status.material_configured)) {
                        failureRow = row.id;
                        throw new Error('runtime apply not confirmed');
                    }
                });
                return self.stationMap.reset();
            }) : null;
        }).catch(function() {
            ui.addNotification('门口机保存或应用未完成', E('p', {}, [
                (failureRow ? '门口机 ' + failureRow + '：' : '') +
                (configSaved ? '配置已保存，但运行时应用尚未确认。' : '配置保存或凭据更新未完成。') +
                '请检查表单和运行时状态；新门口机须先保存并应用，再配置凭据。' +
                (completed.length ? '已更新独立材料：' + completed.join('、') + '；其余未确认。' : '')
            ]), 'danger');
            throw new Error('station save/apply failed');
        });
    },

    handleSave: function() { return this.saveStations(false); },
    handleSaveApply: function() { return this.saveStations(true); },

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
        return this.refreshCredentialStatuses().then(function() { return map.render(); }).then(function(mapNode) {
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
