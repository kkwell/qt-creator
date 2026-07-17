import qbs 1.0

QtcPlugin {
    name: "EtherCATWorkbench"

    Depends { name: "Qt"; submodules: ["widgets"] }
    Depends { name: "EtherCATData" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }
    Depends { name: "EtherCATDevices" }
    Depends { name: "EtherCATProject" }

    files: [
        "builtinpropertypages.cpp",
        "builtinpropertypages.h",
        "coeonlinepage.cpp",
        "coeonlinepage.h",
        "dcpage.cpp",
        "dcpage.h",
        "detailsview.cpp",
        "detailsview.h",
        "esiconfigurationfactory.cpp",
        "esiconfigurationfactory.h",
        "ethercatworkbenchconstants.h",
        "ethercatworkbenchplugin.cpp",
        "ethercatworkbenchtr.h",
        "generalpage.cpp",
        "generalpage.h",
        "processdatapage.cpp",
        "processdatapage.h",
        "startuppage.cpp",
        "startuppage.h",
        "workbenchcontroller.cpp",
        "workbenchcontroller.h",
        "workbenchcommandstrip.cpp",
        "workbenchcommandstrip.h",
        "workbenchmode.cpp",
        "workbenchmode.h",
        "workbenchnavigation.cpp",
        "workbenchnavigation.h",
        "workbenchstatuswidget.cpp",
        "workbenchstatuswidget.h",
        "workbenchtreemodel.cpp",
        "workbenchtreemodel.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatworkbenchtests.cpp",
            "ethercatworkbenchtests.h",
        ]
    }
}
