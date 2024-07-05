add_qtc_plugin(EasyBoard
  DEPENDS QmlDebug
  PLUGIN_DEPENDS Core Debugger ProjectExplorer RemoteLinux
  SOURCES
    easyboardplugin.cpp
)

