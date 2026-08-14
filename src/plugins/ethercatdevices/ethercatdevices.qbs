import qbs 1.0

QtcPlugin {
    name: "EtherCATDevices"

    Depends { name: "Qt"; submodules: ["concurrent"] }
    Depends { name: "EtherCATData" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }

    files: [
        "devicerepository.cpp",
        "devicerepository.h",
        "esiparser.cpp",
        "esiparser.h",
        "ethercatdevicesconstants.h",
        "ethercatdevicesplugin.cpp",
        "ethercatdevicestr.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatdevicestests.cpp",
            "ethercatdevicestests.h",
        ]
    }
}
