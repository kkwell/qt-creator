add_qtc_plugin(EasyBoard
  DEPENDS Utils QmlDebug CmdBridgeClient
  PLUGIN_DEPENDS Core Debugger ProjectExplorer RemoteLinux
  SOURCES
    easyboard.qrc
    easyboardplugin.cpp
    easyboardtr.h
    easyboardwidget.h
    easyboardwidget.cpp
    easyboardmodel.h
    easyboardmodel.cpp
    easyboardbrowser.h
    easyboardbrowser.cpp
    easyboardsettings.h
    easyboardsettings.cpp
    ssdp/netproperty.h
    ssdp/netproperty.cpp
    ssdp/qaesencryption.h
    ssdp/qaesencryption.cpp
)
