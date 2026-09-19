'use strict';
'require form';
'require rpc';
'require ui';
'require view';

var callStatus = rpc.declare({
    object: 'doorfast',
    method: 'status',
    expect: { '': {} }
});

var callMediaCredentials = rpc.declare({
    object: 'doorfast',
    method: 'media_credentials',
    params: ['rtsp_password', 'clear_rtsp_password'],
    expect: { '': {} }
});

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

function encoderCapacity(map, label) {
    return function(sectionId, value) {
        var parsed;
        var enabledStations;

        if (!/^[0-9]+$/.test(value))
            return label + '必须是整数。';
        parsed = Number(value);
        if (!Number.isSafeInteger(parsed) || parsed < 1)
            return label + '必须大于等于 1。';
        enabledStations = map.data.sections(map.config, 'station').filter(
            function(station) {
                return map.data.get(map.config, station['.name'], 'enabled') !== '0';
            }).length;
        if (parsed > enabledStations &&
            (enabledStations > 0 ||
             map.data.get(map.config, 'main', 'media_enabled') === '1'))
            return label + '不能超过已启用门口机数量 ' + enabledStations + '。';
        return true;
    };
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

    option = addMediaOption(section, form.Value, 'media_max_encoders', '最大并发编码器数',
        '同时发布的门口机预览数量，上限由已启用门口机数量决定。');
    option.validate = encoderCapacity(map, '最大并发编码器数');
    option.datatype = 'uinteger';

    option = addMediaOption(section, form.ListValue, 'media_incoming_call_policy', '来电容量策略',
        '容量满时可抢占最早预览，或保留已有预览。');
    option.value('preempt_oldest_preview', '抢占最早预览');
    option.value('preserve_previews', '保留已有预览');

    option = addMediaOption(section, form.Value, 'media_go2rtc_host', 'go2rtc 主机地址',
        '填写 Home Assistant 上 go2rtc 的主机名或 IPv4 地址，不填写协议、端口或路径。');
    option.validate = validateHost;

    option = addMediaOption(section, form.Value, 'media_go2rtc_port', 'go2rtc RTSP 端口',
        'Doorfast 仅连接此主机的 TCP 端口，默认 8554。');
    option.validate = integerInRange(1, 65535, 'RTSP 端口');
    option.datatype = 'port';

    option = addMediaOption(section, form.Value, 'media_rtsp_username', 'RTSP 用户名',
        '与 go2rtc RTSP 认证配置一致。密码单独保存，不写入 UCI。');
    option.validate = validateIdentifier;

    option = addMediaOption(section, form.ListValue, 'media_encoder', 'H.264 编码器',
        '自动模式按设备探测结果选择一次 QSV、VAAPI 或软件编码；已选编码器启动失败时当前预览会失败。');
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

    option = addMediaOption(section, form.Value, 'media_min_free_kib', '最低可用内存（KiB）',
        '可用内存低于此值时拒绝启动预览编码，允许 131072 到 1048576。');
    option.validate = integerInRange(131072, 1048576, '最低可用内存');
    option.datatype = 'uinteger';

    option = addMediaOption(section, form.Value, 'media_preview_timeout', '预览最长时长（秒）',
        '达到时限后主动发送停止预览请求，允许 15 到 600。');
    option.validate = integerInRange(15, 600, '预览最长时长');
    option.datatype = 'uinteger';

    option = addMediaOption(section, form.Value, 'media_first_frame_timeout', '首帧超时（秒）',
        '确认预览后等待首个 JPEG 帧的最长时间，允许 2 到 30。');
    option.validate = integerInRange(2, 30, '首帧超时');
    option.datatype = 'uinteger';

    section = map.section(form.NamedSection, 'main', 'gvs', '媒体凭据');
    section.description = '留空保留原值。页面始终不回显已经保存的密码。修改凭据后需要重启 Doorfast 才会应用到运行中的媒体模块。';

    page.rtspPassword = privateValue(section, '_media_rtsp_password', 'RTSP 密码');
    page.clearRtspPassword = privateFlag(section, '_media_clear_rtsp_password', '清除已保存的 RTSP 密码');

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

    captureMediaCredentials: function() {
        var rtspPassword;
        var clearRtspPassword;

        rtspPassword = fieldValue(this.rtspPassword);
        clearRtspPassword = flagValue(this.clearRtspPassword);
        if (rtspPassword !== '' && clearRtspPassword)
            throw new Error('不能同时填写并清除 RTSP 密码。');
        return {
            rtspPassword: rtspPassword,
            clearRtspPassword: clearRtspPassword,
            changed: rtspPassword !== '' || clearRtspPassword
        };
    },

    mediaCredentialsRequest: function(credentials) {
        if (!credentials.changed)
            return Promise.resolve();
        return callMediaCredentials(credentials.rtspPassword,
            credentials.clearRtspPassword);
    },

    saveMedia: function(apply) {
        var self = this;
        var credentials;

        if (this.mediaMap === null)
            return Promise.resolve();
        try {
            credentials = this.captureMediaCredentials();
        } catch (error) {
            return Promise.reject(error);
        }
        return this.mediaMap.save().then(function() {
            return self.mediaCredentialsRequest(credentials).catch(function(error) {
                ui.addNotification('媒体凭据保存失败', E('p', {}, [
                    error && error.message ? error.message : String(error)
                ]), 'danger');
                return Promise.reject(error);
            });
        }).then(function() {
            return apply ? ui.changes.apply() : null;
        });
    },

    render: function(payload) {
        this.mediaMap = mediaPackageInstalled(payload) ? buildMediaMap(this) : null;
        if (this.mediaMap === null) {
            return E('div', {}, [
                E('h2', {}, ['媒体预览配置']),
                E('div', { 'class': 'alert-message warning' }, [
                    'doorfast-media 未安装或 Doorfast 状态接口不可用。'
                ])
            ]);
        }
        return this.mediaMap.render();
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
