'use strict';
'require form';
'require view';

return view.extend({
    render: function() {
        var map = new form.Map('doorfast-automation', 'Doorfast',
            '保存并应用后，Doorfast 服务会加载新设置。');
        var section = map.section(form.NamedSection, 'main', 'automation',
            '来电自动化');
        var option = section.option(form.Flag, 'call_elev',
            '来电自动向上召梯');

        option.default = option.disabled;
        option.rmempty = false;
        option.description =
            '默认关闭。仅在主机模式下生效；每次有效来电只提交一次向上召梯。协议完成不代表电梯已经到达。';

        var unlock = section.option(form.Flag, 'auto_unlock',
            '来电自动解锁');
        unlock.default = unlock.disabled;
        unlock.rmempty = false;
        unlock.description =
            '默认关闭。主机收到哪台室外机的有效来电，就向哪台室外机发送解锁请求。';
        return map.render();
    }
});
