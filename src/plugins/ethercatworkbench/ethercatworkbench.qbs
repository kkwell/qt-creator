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
        "detailsview.cpp",
        "detailsview.h",
        "ethercatworkbenchconstants.h",
        "ethercatworkbenchplugin.cpp",
        "ethercatworkbenchtr.h",
        "processdatapage.cpp",
        "processdatapage.h",
        "startuppage.cpp",
        "startuppage.h",
        "workbenchcontroller.cpp",
        "workbenchcontroller.h",
        "workbenchmode.cpp",
        "workbenchmode.h",
        "workbenchnavigation.cpp",
        "workbenchnavigation.h",
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
