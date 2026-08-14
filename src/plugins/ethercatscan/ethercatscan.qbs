import qbs 1.0

QtcPlugin {
    name: "EtherCATScan"

    Depends { name: "Qt"; submodules: ["widgets"] }
    Depends { name: "EtherCATData" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }
    Depends { name: "EtherCATDevices" }
    Depends { name: "EtherCATProject" }
    Depends { name: "EtherCATWorkbench" }

    files: [
        "ethercatscanconstants.h",
        "ethercatscanplugin.cpp",
        "ethercatscantr.h",
        "mockscanprovider.cpp",
        "mockscanprovider.h",
        "scanpropertypages.cpp",
        "scanpropertypages.h",
        "scanworkflow.cpp",
        "scanworkflow.h",
        "topologycomparison.cpp",
        "topologycomparison.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatscantests.cpp",
            "ethercatscantests.h",
        ]
    }
}
