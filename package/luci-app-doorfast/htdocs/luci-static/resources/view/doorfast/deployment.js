'use strict';
'require form';
'require view';

return view.extend({
    render: function() {
        var map = new form.Map('doorfast', 'Doorfast 部署配置',
            '保存并应用后，Doorfast 服务会加载新设置。主机模式只应在隔离网络完成验收后启用。');
        var section = map.section(form.NamedSection, 'main', 'gvs', '主服务与模式');
        var option;
        var validateIdentity = function(sectionId, value) {
            var match = /^IS:(\d+)-(\d+)-(\d+)-(\d+)$/.exec(value);
            var room;

            if (match === null)
                return 'GVS 逻辑身份格式应为 IS:楼栋-单元-房间-分机。';
            room = Number(match[3]);
            if (Number(match[1]) < 1 || Number(match[1]) > 99 ||
                Number(match[2]) < 1 || Number(match[2]) > 9 ||
                room < 101 || room > 6332 || room % 100 === 0 ||
                room % 100 > 32 || Number(match[4]) < 1 ||
                Number(match[4]) > 4)
                return 'GVS 逻辑身份中的楼栋、单元、房间或分机号无效。';
            return true;
        };
        var validateAccessMaterial = function(sectionId, value) {
            if (value === '')
                return true;
            if (value.length !== 16)
                return '门禁材料必须为 16 位十六进制字符串。';
            if (!/^[0-9a-fA-F]+$/.test(value))
                return '门禁材料只能包含十六进制字符。';
            return true;
        };
        var validateMulticastAddress = function(sectionId, value) {
            var parts = value.split('.');

            if (parts.length !== 4 || parts.some(function(part) {
                return !/^(?:0|[1-9][0-9]{0,2})$/.test(part) ||
                    Number(part) > 255;
            }))
                return '必须填写有效的 IPv4 地址。';
            if (Number(parts[0]) < 224 || Number(parts[0]) > 239)
                return '自定义地址必须位于 224.0.0.0/4 IPv4 组播范围。';
            return true;
        };

        option = section.option(form.Flag, 'enabled', '启用 Doorfast');
        option.default = option.disabled;
        option.rmempty = false;

        option = section.option(form.Flag, 'active_host', '启用主机模式');
        option.default = option.disabled;
        option.rmempty = false;
        option.description =
            '关闭时为被动观察，不绑定室内机 IP，也不提交协议事务。启用后显示主机模式配置；LuCI 不提供接听、挂断、开锁或召梯操作。';

        section = map.section(form.NamedSection, 'main', 'gvs', '被动观察配置');

        option = section.option(form.Value, 'passive_interface', '被动观察接口');
        option.rmempty = false;
        option.depends('active_host', '0');
        option.description = '仅用于抓取门禁网络报文；被动观察不绑定室内机 IP。';

        option = section.option(form.Flag, 'capture_promiscuous', '启用混杂模式抓包');
        option.default = option.disabled;
        option.rmempty = false;
        option.depends('active_host', '0');
        option.description = '只在交换网络允许且确有抓包需要时启用。';

        option = section.option(form.Value, 'gvs_local_address', 'GVS 逻辑身份');
        option.datatype = 'string';
        option.rmempty = false;
        option.placeholder = 'IS:2-1-101-1';
        option.depends('active_host', '0');
        option.description = '正确格式：IS:楼栋-单元-房间-分机，例如 IS:2-1-101-1。被动观察只按此身份筛选报文，不绑定室内机 IP。';
        option.validate = validateIdentity;

        section = map.section(form.NamedSection, 'main', 'gvs', '主机模式配置');

        option = section.option(form.Value, 'host_interface', '主机模式接口');
        option.rmempty = false;
        option.depends('active_host', '1');
        option.description = '填写物理口、VLAN、bridge 或 bond 的实际接口名。服务只在此接口临时添加室内机地址。';

        option = section.option(form.Value, 'gvs_local_address', 'GVS 逻辑身份');
        option.datatype = 'string';
        option.rmempty = false;
        option.placeholder = 'IS:2-1-101-1';
        option.depends('active_host', '1');
        option.description = '正确格式：IS:楼栋-单元-房间-分机，例如 IS:2-1-101-1。主机模式必须填写有效身份。';
        option.validate = validateIdentity;

        option = section.option(form.Value, 'indoor_ipaddr', '室内机 IP 地址');
        option.datatype = 'ip4addr';
        option.rmempty = true;
        option.depends('active_host', '1');
        option.description = '仅在主机模式使用。留空时按 GVS 逻辑身份自动推导；填写后覆盖自动值。';

        option = section.option(form.Value, 'indoor_netmask', '室内机子网掩码');
        option.datatype = 'ip4addr';
        option.rmempty = true;
        option.depends('active_host', '1');
        option.description = '仅在主机模式使用。厂商材料未证明通用自动掩码，首次请按现场室内机填写。';

        option = section.option(form.ListValue, 'multicast_mode', '组播地址模式');
        option.value('auto', '自动推导');
        option.value('custom', '自定义');
        option.default = 'auto';
        option.rmempty = false;
        option.depends('active_host', '1');
        option.description = '自动模式按 GVS 逻辑身份推导组播地址。';

        option = section.option(form.Value, 'multicast_address', '自定义组播地址');
        option.datatype = 'ip4addr';
        option.rmempty = false;
        option.depends({active_host: '1', multicast_mode: 'custom'});
        option.description = '自定义模式必须填写有效的 IPv4 组播地址。';
        option.validate = validateMulticastAddress;

        option = section.option(form.Value, 'access_material', '门禁材料（可选）');
        option.datatype = 'hexstring';
        option.password = true;
        option.rmempty = true;
        option.depends('active_host', '1');
        option.description = '仅接受 16 位十六进制材料；留空不会生成默认值。';
        option.validate = validateAccessMaterial;

        option = section.option(form.Value, 'sync_mini1_secretkey',
            'Mini 1 同步密钥（可选）');
        option.password = true;
        option.rmempty = true;
        option.depends('active_host', '1');
        option.description = '厂商同步键 sync_mini1_secretkey；仅在有现场确认值时填写，填写后进入周期 91/03。';

        option = section.option(form.Value, 'sync_mini2_secretkey',
            'Mini 2 同步密钥（可选）');
        option.password = true;
        option.rmempty = true;
        option.depends('active_host', '1');
        option.description = '厂商同步键 sync_mini2_secretkey；仅在有现场确认值时填写，填写后进入周期 91/03。';

        section = map.section(form.NamedSection, 'main', 'gvs', '可选上行配置');
        option = section.option(form.Value, 'uplink_interface', '上行接口（可选）');
        option.rmempty = true;
        option.description = '与门禁接口分离的管理或上行网络接口；留空不会改变门禁接口。';

        return map.render();
    }
});
