add_qtc_plugin(EasyBoard
  DEPENDS QmlDebug
  PLUGIN_DEPENDS Core Debugger ProjectExplorer RemoteLinux
  SOURCES
    easyboard.qrc
    easyboardplugin.cpp
    easyboardtr.h
)

