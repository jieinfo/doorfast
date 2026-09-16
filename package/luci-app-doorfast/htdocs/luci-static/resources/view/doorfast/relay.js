'use strict';
'require form';
'require view';

return view.extend({
    render: function() {
        var map = new form.Map('doorfast-events', 'HA 事件 relay',
            'relay 只把本地事件发送给 Home Assistant；状态轮询仍是权威来源。');
        var section = map.section(form.NamedSection, 'main', 'relay', '事件推送');
        var option;

        option = section.option(form.Flag, 'enabled', '启用事件 relay');
        option.default = option.disabled;
        option.rmempty = false;

        option = section.option(form.Value, 'url', 'Home Assistant 地址');
        option.datatype = 'url';
        option.rmempty = false;
        option.placeholder = 'http://ha.example:8123';
        option.description = '只填写 HTTP 或 HTTPS authority，不包含路径。HTTP 仅适用于可信内网。';
        option.validate = function(sectionId, value) {
            if (value === '')
                return true;
            if (!/^https?:\/\/[^/:?#]+(?::[0-9]+)?$/.test(value))
                return '必须填写不含路径的 HTTP 或 HTTPS 地址。';
            return true;
        };

        option = section.option(form.Value, 'entry_id', 'Home Assistant 配置项 ID');
        option.datatype = 'string';
        option.rmempty = false;

        option = section.option(form.Value, 'token', 'Home Assistant 长期访问令牌');
        option.password = true;
        option.rmempty = true;
        option.cfgvalue = function() { return ''; };
        option.description = '填写后会写入仅 root 可读的本地文件，并从配置中立即删除；为空时保留当前令牌。';

        option = section.option(form.Value, 'ca_file', 'CA 证书文件');
        option.datatype = 'file';
        option.rmempty = false;

        return map.render();
    }
});
