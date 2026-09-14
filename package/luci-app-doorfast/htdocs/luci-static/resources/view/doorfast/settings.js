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
        return map.render();
    }
});
