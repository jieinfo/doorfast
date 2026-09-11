# Unnumbered bridge preflight

Release r22 accepts a LuCI/netifd interface that explicitly selects the configured
bridge with `option device` and `option proto 'none'`. This fixes r21 reporting
`bridge_managed` for the field's addressless `door` interface.

The exception permits only one matching interface, exactly one device and protocol,
and optional `multipath off`, `ipv6 0`, or `auto 1`. Missing or duplicate protocol,
static/DHCP protocol, member-interface bindings, address options, and unknown options
remain rejected. Conservative rejection also applies to unsupported UCI syntax.

The logical interface name is checked against firewall and DHCP configuration in
addition to the bridge/member names. Live address, membership, STP, multicast,
passive-mode and disk checks remain in force. No network configuration is applied.

After installing r22, rerun `doorfast --preflight /etc/config/doorfast-deployment`
before enabling recording. This source fix does not constitute field acceptance;
the installed r21 recorder remains disabled until the updated package is verified.
