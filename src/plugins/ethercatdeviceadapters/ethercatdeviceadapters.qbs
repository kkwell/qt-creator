import qbs 1.0

QtcPlugin {
    name: "EtherCATDeviceAdapters"

    Depends { name: "EtherCATData" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }

    files: [
        "adapterpackagerepository.cpp",
        "adapterpackagerepository.h",
        "ethercatdeviceadaptersplugin.cpp",
    ]

    QtcTestFiles {
        files: [
            "ethercatdeviceadapterstests.cpp",
            "ethercatdeviceadapterstests.h",
            "testdata/device-adapter-v3-contract.fixture.json",
            "testdata/device-adapter-v3.schema.json",
        ]
    }
}
