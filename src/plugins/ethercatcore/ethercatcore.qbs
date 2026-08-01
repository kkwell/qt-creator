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
        "runtimepackagecompilercodec.cpp",
        "runtimepackagecompilercodec.h",
        "runtimepackagecompilerprovider.cpp",
        "runtimepackagecompilerprovider.h",
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
            "testdata/api042/adapter-bundle.json",
            "testdata/api042/compile-request.json",
            "testdata/api042/compile-result.json",
            "testdata/api042/controller-features.json",
            "testdata/api042/policy-template.json",
            "testdata/api042/runtime.st",
            "testdata/api042/sign-request.json",
            "testdata/api042/sign-response.json",
            "testdata/api042/sv630n.ecdev.yaml",
            "testdata/api042/target-profile.json",
            "testdata/api042/topology-evidence.json",
            "testdata/api042/xb6.ecdev.yaml",
        ]
    }
}
