# GVS 同步链路分析范围

- `auth`: granted。用户授权分析其本地持有的 MT8157 APK 解包材料，并在 Doorfast 私有工作树中实现兼容模块。
- `in_scope`: `IndoorSyncBusiness.smali`、`com/gvs/general/protocol/c.smali`、Doorfast 的身份、在线维护、同步数据、被动运行时路由与本地 UCI 状态文件。
- `network_profile`: offline_only。本阶段只读取本地文件、生成内存报文并执行合成回放。
- `out_of_scope`: 向门口机、室内机或其他真实 GVS 设备发包；宣称设备已经接受报文；提供或验证真实公共头认证字段。
- `data_handling`: 测试只使用合成地址、字段名和值；运行日志不记录家庭逻辑地址和同步值。允许守护进程把版本号原子写入配置指定的 `/etc/config/doorfast-*` 状态文件。运行时状态查询仅返回阶段、角色、计数和最近一次同步处理结果，不返回适配器字段名、字段值或家庭逻辑地址。
