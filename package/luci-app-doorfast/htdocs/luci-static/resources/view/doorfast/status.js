'use strict';
'require dom';
'require form';
'require poll';
'require rpc';
'require ui';
'require view';
'require doorfast.status_model as statusModel';

var callStatus = rpc.declare({
    object: 'doorfast',
    method: 'status',
    expect: { '': {} }
});

var callMediaCredentials = rpc.declare({
    object: 'doorfast',
    method: 'media_credentials',
    params: [
        'rtsp_password', 'relay_token', 'clear_rtsp_password',
        'clear_relay_token'
    ],
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

function integerInRange(minimum, maximum, label) {
    return function(sectionId, value) {
        var parsed;

        if (!/^[0-9]+$/.test(value))
            return label + '必须是整数。';
        parsed = Number(value);
        if (!Number.isSafeInteger(parsed) || parsed < minimum || parsed > maximum)
            return label + '必须在 ' + minimum + ' 到 ' + maximum + ' 之间。';
        return true;
    };
}

function validateStation(sectionId, value) {
    var match = /^([0-9a-fA-F]{2}):([0-9a-fA-F]{2}):([0-9a-fA-F]{2}):00:([0-9a-fA-F]{2}):00$/.exec(value);
    var building;
    var unit;
    var station;

    if (match === null || Number.parseInt(match[1], 16) !== 0x32)
        return '门口机逻辑地址必须是 32:bb:uu:00:gg:00 格式。';
    building = Number.parseInt(match[2], 16);
    unit = Number.parseInt(match[3], 16);
    station = Number.parseInt(match[4], 16);
    if (building === 0 || unit === 0 || station === 0 ||
        (building >> 4) > 9 || (building & 0x0f) > 9 ||
        (unit >> 4) > 9 || (unit & 0x0f) > 9 ||
        (station >> 4) > 9 || (station & 0x0f) > 9)
        return '门口机逻辑地址中的楼栋、单元或门口机号无效。';
    return true;
}

function validateHost(sectionId, value) {
    if (!/^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,61}[A-Za-z0-9])?$/.test(value) ||
        value.indexOf('..') !== -1)
        return '地址只能填写主机名或 IPv4 地址，不能包含协议、路径、端口或用户信息。';
    return true;
}

function validateIdentifier(sectionId, value) {
    if (!/^[A-Za-z0-9_.-]{1,32}$/.test(value))
        return '只能包含 ASCII 字母、数字、下划线、连字符或句点，且长度不超过 32。';
    return true;
}

function validateStreamName(sectionId, value) {
    if (!/^[A-Za-z0-9_-]{1,64}$/.test(value))
        return '流名称只能包含 ASCII 字母、数字、下划线或连字符，且长度不超过 64。';
    return true;
}

function validateRelayUrl(sectionId, value) {
    var match;
    var port;

    if (value === '')
        return true;
    match = /^https?:\/\/([A-Za-z0-9](?:[A-Za-z0-9.-]{0,61}[A-Za-z0-9])?)(?::([0-9]{1,5}))?$/.exec(value);
    if (match === null)
        return 'relay 地址必须是 http:// 或 https:// 的主机地址，可带端口但不能包含路径。';
    if (match[2] !== undefined) {
        port = Number(match[2]);
        if (!Number.isSafeInteger(port) || port < 1 || port > 65535)
            return 'relay 端口必须在 1 到 65535 之间。';
    }
    return true;
}

function privateValue(section, name, title) {
    var option = section.option(form.Value, name, title);

    option.password = true;
    option.rmempty = true;
    option.cfgvalue = function() { return ''; };
    option.write = function() {};
    option.remove = function() {};
    return option;
}

function privateFlag(section, name, title) {
    var option = section.option(form.Flag, name, title);

    option.default = option.disabled;
    option.rmempty = false;
    option.cfgvalue = function() { return option.disabled; };
    option.write = function() {};
    option.remove = function() {};
    return option;
}

function addMediaOption(section, type, name, title, description) {
    var option = section.option(type, name, title);

    option.description = description;
    return option;
}

function buildMediaMap(page) {
    var map = new form.Map('doorfast', '媒体预览配置',
        'Doorfast 只向 Home Assistant 的 go2rtc RTSP 入口发送已验证的预览视频。媒体模块默认关闭。');
    var section = map.section(form.NamedSection, 'main', 'gvs', '主动预览');
    var option;

    option = addMediaOption(section, form.Flag, 'media_enabled', '启用媒体预览',
        '需要主机模式、正确的门口机逻辑地址和 go2rtc 地址。关闭时不会发送主动预览请求。');
    option.default = option.disabled;
    option.rmempty = false;

    option = addMediaOption(section, form.Value, 'media_station_address', '门口机逻辑地址',
        '从抓包或物业设备表填写精确的六字节地址，不可由室内机 GVS 逻辑身份推导。');
    option.placeholder = '32:02:01:00:02:00';
    option.validate = validateStation;

    option = addMediaOption(section, form.Value, 'media_station_ipv4', '门口机 IPv4 地址（可选）',
        '留空时只使用新鲜的 07/86 发现路由；填写后作为明确的单播回退地址。');
    option.datatype = 'ip4addr';
    option.rmempty = true;

    option = addMediaOption(section, form.Value, 'media_go2rtc_host', 'go2rtc 主机地址',
        '填写 Home Assistant 上 go2rtc 的主机名或 IPv4 地址，不填写协议、端口或路径。');
    option.validate = validateHost;

    option = addMediaOption(section, form.Value, 'media_go2rtc_port', 'go2rtc RTSP 端口',
        'Doorfast 仅连接此主机的 TCP 端口，默认 8554。');
    option.validate = integerInRange(1, 65535, 'RTSP 端口');
    option.datatype = 'port';

    option = addMediaOption(section, form.Value, 'media_stream_name', 'go2rtc 流名称',
        '必须与 go2rtc 中预先创建的空流一致。');
    option.validate = validateStreamName;

    option = addMediaOption(section, form.Value, 'media_rtsp_username', 'RTSP 用户名',
        '与 go2rtc RTSP 认证配置一致。密码单独保存，不写入 UCI。');
    option.validate = validateIdentifier;

    option = addMediaOption(section, form.ListValue, 'media_encoder', 'H.264 编码器',
        '自动选择依次尝试 QSV、VAAPI 和软件编码；明确选择的不可用编码器会拒绝启动。');
    option.value('auto', '自动');
    option.value('software', '软件 libx264');
    option.value('vaapi', 'VAAPI');
    option.value('qsv', 'Intel QSV');

    option = addMediaOption(section, form.ListValue, 'media_resolution', '输出分辨率',
        '源分辨率通常为 480x640；尺寸变化会重新建立发布。');
    option.value('source', '源分辨率');
    option.value('480x640', '480x640');
    option.value('360x480', '360x480');
    option.value('240x320', '240x320');

    option = addMediaOption(section, form.ListValue, 'media_fps', '帧率',
        '每秒输出帧数。');
    ['5', '8', '10', '12', '15'].forEach(function(value) { option.value(value); });

    option = addMediaOption(section, form.Value, 'media_bitrate_kbps', '目标码率（Kbps）',
        '允许 256 到 2000。');
    option.validate = integerInRange(256, 2000, '目标码率');
    option.datatype = 'uinteger';

    option = addMediaOption(section, form.ListValue, 'media_profile', 'H.264 Profile',
        '默认 Baseline 便于浏览器兼容。');
    option.value('baseline', 'Baseline');
    option.value('main', 'Main');

    option = addMediaOption(section, form.Value, 'media_relay_url', '媒体事件 relay 地址（可选）',
        '填写 http:// 或 https:// 的主机地址，可带端口但不能包含路径。relay 令牌单独保存。');
    option.rmempty = true;
    option.validate = validateRelayUrl;

    section = map.section(form.NamedSection, 'main', 'gvs', '媒体凭据');
    section.description = '留空保留原值。页面始终不回显已经保存的密码或 relay 令牌。修改凭据后需要重启 Doorfast 才会应用到运行中的媒体模块。';

    page.rtspPassword = privateValue(section, '_media_rtsp_password', 'RTSP 密码');
    page.clearRtspPassword = privateFlag(section, '_media_clear_rtsp_password', '清除已保存的 RTSP 密码');
    page.relayToken = privateValue(section, '_media_relay_token', '媒体 relay Bearer 令牌');
    page.clearRelayToken = privateFlag(section, '_media_clear_relay_token', '清除已保存的媒体 relay 令牌');

    return map;
}

function mediaPackageInstalled(payload) {
    return payload !== null && typeof payload === 'object' &&
        payload.media !== null && typeof payload.media === 'object' &&
        payload.media.installed === true;
}

function fieldValue(option) {
    var value = option.formvalue('main');
    return typeof value === 'string' ? value : '';
}

function flagValue(option) {
    var value = option.formvalue('main');
    return value === true || value === '1';
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

    mediaCredentialsRequest: function() {
        var rtspPassword;
        var relayToken;
        var clearRtspPassword;
        var clearRelayToken;

        if (this.mediaMap === null)
            return Promise.resolve();
        rtspPassword = fieldValue(this.rtspPassword);
        relayToken = fieldValue(this.relayToken);
        clearRtspPassword = flagValue(this.clearRtspPassword);
        clearRelayToken = flagValue(this.clearRelayToken);
        if (rtspPassword !== '' && clearRtspPassword)
            return Promise.reject(new Error('不能同时填写并清除 RTSP 密码。'));
        if (relayToken !== '' && clearRelayToken)
            return Promise.reject(new Error('不能同时填写并清除媒体 relay 令牌。'));
        if (rtspPassword === '' && relayToken === '' &&
            !clearRtspPassword && !clearRelayToken)
            return Promise.resolve();
        return callMediaCredentials(rtspPassword, relayToken,
            clearRtspPassword, clearRelayToken);
    },

    saveMedia: function(apply) {
        var self = this;

        if (this.mediaMap === null)
            return Promise.resolve();
        return this.mediaMap.save(function() {
            return self.mediaCredentialsRequest();
        }).then(function() {
            return apply ? ui.changes.apply() : null;
        }).then(function() {
            return self.refreshStatus();
        });
    },

    render: function(payload) {
        var self = this;

        this.lastStatus = payload;
        this.statusNode = E('div', {});
        this.mediaMap = mediaPackageInstalled(payload) ? buildMediaMap(this) : null;
        this.renderCurrent(payload, false);
        poll.add(function() { return self.refreshStatus(); }, 5);
        if (this.mediaMap === null) {
            return E('div', {}, [
                E('h2', {}, ['Doorfast']),
                E('p', {}, ['显示被动观察、同步维护与离线模拟控制状态。安装 doorfast-media 后可在此配置主动预览。']),
                this.statusNode
            ]);
        }
        return this.mediaMap.render().then(function(mediaNode) {
            return E('div', {}, [
                E('h2', {}, ['Doorfast']),
                E('p', {}, ['显示 Doorfast 状态，并配置可选的主动预览媒体模块。']),
                self.statusNode,
                mediaNode
            ]);
        });
    },

    handleSaveApply: function(ev, mode) {
        return this.saveMedia(true);
    },

    handleSave: function() {
        return this.saveMedia(false);
    },

    handleReset: function() {
        return this.mediaMap === null ? null : this.mediaMap.reset();
    }
});
