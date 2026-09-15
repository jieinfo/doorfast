'use strict';
'require form';
'require view';

return view.extend({
    render: function() {
        var map = new form.Map('doorfast-events', 'HA 事件 relay',
            'relay 只把本地事件发送给 Home Assistant；状态轮询仍是权威来源。');
        var section = map.section(form.NamedSection, 'main', 'relay', 'HTTPS 推送');
        var option;

        option = section.option(form.Flag, 'enabled', '启用事件 relay');
        option.default = option.disabled;
        option.rmempty = false;

        option = section.option(form.Value, 'url', 'Home Assistant HTTPS 地址');
        option.datatype = 'url';
        option.rmempty = false;
        option.placeholder = 'https://ha.example:8443';
        option.description = '只填写 HTTPS authority，不包含路径。';
        option.validate = function(sectionId, value) {
            if (value === '')
                return true;
            if (!/^https:\/\/[^/:?#]+(?::[0-9]+)?$/.test(value))
                return '必须填写不含路径的 HTTPS 地址。';
            return true;
        };

        option = section.option(form.Value, 'entry_id', 'Home Assistant 配置项 ID');
        option.datatype = 'string';
        option.rmempty = false;

        option = section.option(form.Value, 'token_file', '令牌文件');
        option.datatype = 'file';
        option.rmempty = false;
        option.description = '令牌必须保存于 root 所有、权限 0600 的本地文件中。';

        option = section.option(form.Value, 'ca_file', 'CA 证书文件');
        option.datatype = 'file';
        option.rmempty = false;

        return map.render();
    }
});
