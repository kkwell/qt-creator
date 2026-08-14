# Built-in EtherCAT ESI library

Place product-qualified EtherCAT Slave Information XML files in this
directory. Embed Labs copies the directory into its application resources and
indexes every `.xml` file at startup.

Users can add their own vendor XML files without changing the application
bundle by placing them in:

```text
Core::ICore::userResourcePath()/ethercat/esi/library
```

The Device Repository page shows the resolved path and also supports immediate
manual import.
