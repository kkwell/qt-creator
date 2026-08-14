import qbs 1.0

QtcPlugin {
    name: "EasyBoard"

    Depends { name: "Qt"; submodules: ["network", "widgets"] }
    Depends { name: "CmdBridgeClient" }
    Depends { name: "QmlDebug" }
    Depends { name: "QtTaskTree" }
    Depends { name: "Utils" }

    Depends { name: "Core" }
    Depends { name: "Debugger" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "RemoteLinux" }

    files: [
        "boardsWidget/t113s.cpp",
        "boardsWidget/t113s.h",
        "easyboard.qrc",
        "easyboard_export.h",
        "easyboardbrowser.cpp",
        "easyboardbrowser.h",
        "easyboardmodel.cpp",
        "easyboardmodel.h",
        "easyboardplugin.cpp",
        "easyboardsettings.cpp",
        "easyboardsettings.h",
        "easyboardstruct.h",
        "easyboardtr.h",
        "easyboardwidget.cpp",
        "easyboardwidget.h",
        "newboarddialog.cpp",
        "newboarddialog.h",
        "ssdp/netproperty.cpp",
        "ssdp/netproperty.h",
        "ssdp/qaesencryption.cpp",
        "ssdp/qaesencryption.h",
    ]
}
