add_qtc_plugin(EasyBoard
  DEPENDS Utils QmlDebug CmdBridgeClient
  PLUGIN_DEPENDS Core Debugger ProjectExplorer RemoteLinux
  SOURCES
    easyboard.qrc
    easyboardplugin.cpp
    easyboardtr.h
    easyboard_export.h
    easyboardstruct.h
    easyboardwidget.h
    easyboardwidget.cpp
    easyboardmodel.h
    easyboardmodel.cpp
    easyboardbrowser.h
    easyboardbrowser.cpp
    easyboardsettings.h
    easyboardsettings.cpp
    newboarddialog.h
    newboarddialog.cpp
    ssdp/netproperty.h
    ssdp/netproperty.cpp
    ssdp/qaesencryption.h
    ssdp/qaesencryption.cpp
)
