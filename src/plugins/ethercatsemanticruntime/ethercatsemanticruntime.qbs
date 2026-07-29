import qbs 1.0

QtcPlugin {
    name: "EtherCATSemanticRuntime"

    Depends { name: "Qt"; submodules: ["core"] }
    Depends { name: "EtherCATData" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }
    Depends { name: "EtherCATProject" }

    files: [
        "ethercatsemanticruntimeplugin.cpp",
        "semanticruntimeexecutor.cpp",
        "semanticruntimeexecutor.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatsemanticruntimetests.cpp",
            "ethercatsemanticruntimetests.h",
        ]
    }
}
