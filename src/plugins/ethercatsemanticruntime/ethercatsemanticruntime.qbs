import qbs 1.0

QtcPlugin {
    name: "EtherCATSemanticRuntime"

    Depends { name: "Qt"; submodules: ["core"] }
    Depends { name: "EtherCATData" }
    Depends { name: "qtcMonocypher" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }
    Depends { name: "EtherCATProjectCompiler" }
    Depends { name: "EtherCATProject" }

    pluginTestDepends: ["EtherCATDeviceAdapters", "EtherCATProductApi"]

    files: [
        "canonicaljson_p.cpp",
        "canonicaljson_p.h",
        "ecfgconfiguration_p.cpp",
        "ecfgconfiguration_p.h",
        "ecpkgcontainer.cpp",
        "ecpkgcontainer.h",
        "ed25519verifier.cpp",
        "ed25519verifier.h",
        "ethercatsemanticruntimeplugin.cpp",
        "productiontruststore_p.cpp",
        "productiontruststore_p.h",
        "readonlysemanticbindingfactory_p.cpp",
        "readonlysemanticbindingfactory_p.h",
        "runtimepackageevidence_p.cpp",
        "runtimepackageevidence_p.h",
        "runtimepackageevidencerepository_p.cpp",
        "runtimepackageevidencerepository_p.h",
        "runtimepackageactivationservice_p.cpp",
        "runtimepackageactivationservice_p.h",
        "runtimepackageactivationjournalcodec_p.cpp",
        "runtimepackageactivationjournalcodec_p.h",
        "semanticactiondefinitions_p.cpp",
        "semanticactiondefinitions_p.h",
        "semanticactionplan_p.cpp",
        "semanticactionplan_p.h",
        "semanticactionruntimefactory_p.cpp",
        "semanticactionruntimefactory_p.h",
        "semanticbindingartifact_p.cpp",
        "semanticbindingartifact_p.h",
        "semanticoperationjournal_p.cpp",
        "semanticoperationjournal_p.h",
        "semanticruntimeexecutor.cpp",
        "semanticruntimeexecutor.h",
        "signedecpkgmanifest_p.cpp",
        "signedecpkgmanifest_p.h",
        "verifiedecpkgstore_p.cpp",
        "verifiedecpkgstore_p.h",
    ]

    QtcTestFiles {
        cpp.defines: outer.concat(
            'ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR="' + sourceDirectory + '"')
        files: [
            "ethercatsemanticruntimetests.cpp",
            "ethercatsemanticruntimetests.h",
            "testdata/api035-manifest.json",
            "testdata/api036-manifest.json",
            "testdata/api038/eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub",
            "testdata/api038/project.json",
            "testdata/api038/semantic-action-definitions-v1.json",
            "testdata/api038/semantic-binding-v2.json",
            "testdata/api038/three-slave-manual-control-cfg3701.ecpkg",
        ]
    }
}
