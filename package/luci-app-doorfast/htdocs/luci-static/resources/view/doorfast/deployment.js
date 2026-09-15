'use strict';
'require form';
'require view';

return view.extend({
    render: function() {
        var map = new form.Map('doorfast', 'Doorfast 部署配置',
            '保存并应用后，Doorfast 服务会加载新设置。主机模式只应在隔离网络完成验收后启用。');
        var section = map.section(form.NamedSection, 'main', 'gvs', '主服务');
        var option;

        option = section.option(form.Flag, 'enabled', '启用 Doorfast');
        option.default = option.disabled;
        option.rmempty = false;

        option = section.option(form.Flag, 'active_host', '启用主机模式');
        option.default = option.disabled;
        option.rmempty = false;
        option.description =
            '启用主机模式后可由 Home Assistant 经受控本地接口提交协议事务；LuCI 不提供接听、挂断、开锁或召梯操作。';

        option = section.option(form.Value, 'gvs_interface', '门禁网络接口');
        option.rmempty = false;
        option.description = '填写物理口、VLAN、bridge 或 bond 的实际接口名。';

        option = section.option(form.Value, 'uplink_interface', '上行接口（可选）');
        option.rmempty = true;

        option = section.option(form.Value, 'gvs_local_address', '本机逻辑身份');
        option.datatype = 'string';
        option.rmempty = false;
        option.placeholder = 'IS:2-1-101-1';
        option.description = '格式为 IS:楼栋-单元-房间-分机，用于入站帧筛选。';

        option = section.option(form.Value, 'access_material', '门禁材料（可选）');
        option.datatype = 'hexstring';
        option.password = true;
        option.rmempty = true;
        option.description = '仅接受 16 位十六进制材料；留空不会生成默认值。';
        option.validate = function(sectionId, value) {
            if (value === '')
                return true;
            if (value.length !== 16)
                return '门禁材料必须为 16 位十六进制字符串。';
            if (!/^[0-9a-fA-F]+$/.test(value))
                return '门禁材料只能包含十六进制字符。';
            return true;
        };

        return map.render();
    }
});
