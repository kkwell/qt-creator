add_qtc_plugin(EasyBoard
  DEPENDS QmlDebug
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
)

