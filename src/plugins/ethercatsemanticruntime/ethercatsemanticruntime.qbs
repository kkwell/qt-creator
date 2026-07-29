import qbs 1.0

QtcPlugin {
    name: "EtherCATSemanticRuntime"

    Depends { name: "Qt"; submodules: ["core"] }
    Depends { name: "EtherCATData" }
    Depends { name: "qtcMonocypher" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }
    Depends { name: "EtherCATProject" }

    files: [
        "canonicaljson_p.cpp",
        "canonicaljson_p.h",
        "ecpkgcontainer.cpp",
        "ecpkgcontainer.h",
        "ed25519verifier.cpp",
        "ed25519verifier.h",
        "ethercatsemanticruntimeplugin.cpp",
        "semanticruntimeexecutor.cpp",
        "semanticruntimeexecutor.h",
        "signedecpkgmanifest_p.cpp",
        "signedecpkgmanifest_p.h",
    ]

    QtcTestFiles {
        cpp.defines: outer.concat(
            'ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR="' + sourceDirectory + '"')
        files: [
            "ethercatsemanticruntimetests.cpp",
            "ethercatsemanticruntimetests.h",
            "testdata/api035-manifest.json",
            "testdata/api036-manifest.json",
        ]
    }
}
