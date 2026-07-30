import qbs 1.0

QtcPlugin {
    name: "EtherCATCore"

    Depends { name: "Qt"; submodules: ["widgets"] }
    Depends { name: "EtherCATData" }
    Depends { name: "Utils" }
    Depends { name: "Core" }

    files: [
        "automationservice.cpp",
        "automationservice.h",
        "ethercatcore_global.h",
        "ethercatcoreconstants.h",
        "ethercatcoreplugin.cpp",
        "ethercatcoresettings.cpp",
        "ethercatcoresettings.h",
        "ethercatcoretr.h",
        "manualcontrolcontract.cpp",
        "manualcontrolcontract.h",
        "providerregistry.cpp",
        "providerregistry.h",
        "providers.cpp",
        "providers.h",
        "runtimepackageactivationservice.h",
        "selectionservice.cpp",
        "selectionservice.h",
        "semanticruntimeservice.cpp",
        "semanticruntimeservice.h",
        "stateservice.cpp",
        "stateservice.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatcoretests.cpp",
            "ethercatcoretests.h",
        ]
    }
}
