# Doorfast HTTP bridge smoke test

The package installs `/www/cgi-bin/doorfast`. With uhttpd serving CGI scripts,
the read-only status endpoint is:

```text
http://<doorfast-host>/cgi-bin/doorfast/api/v1/status
```

After installing the package, run the smoke test from a machine that can reach
the host:

```text
python3 tests/run_doorfast_http_status.py http://<doorfast-host>/cgi-bin/doorfast
```

The script only calls `status`; it does not unlock, answer, hang up, or call an
elevator. Test those actions from Home Assistant after confirming the device is
in active host mode and the local network is isolated.
