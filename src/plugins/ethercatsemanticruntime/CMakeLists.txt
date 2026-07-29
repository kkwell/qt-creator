add_qtc_plugin(EtherCATSemanticRuntime
  DEPENDS EtherCATData Qt::Core Utils qtcMonocypher
  PLUGIN_DEPENDS
    Core
    EtherCATCore
    EtherCATProject
  SOURCES
    canonicaljson_p.cpp
    canonicaljson_p.h
    ecfgconfiguration_p.cpp
    ecfgconfiguration_p.h
    ecpkgcontainer.cpp
    ecpkgcontainer.h
    ed25519verifier.cpp
    ed25519verifier.h
    ethercatsemanticruntimeplugin.cpp
    semanticbindingartifact_p.cpp
    semanticbindingartifact_p.h
    semanticruntimeexecutor.cpp
    semanticruntimeexecutor.h
    signedecpkgmanifest_p.cpp
    signedecpkgmanifest_p.h
    verifiedecpkgstore_p.cpp
    verifiedecpkgstore_p.h
)

extend_qtc_plugin(EtherCATSemanticRuntime
  CONDITION WITH_TESTS
  DEFINES
    ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES
    ethercatsemanticruntimetests.cpp
    ethercatsemanticruntimetests.h
    testdata/api035-manifest.json
    testdata/api036-manifest.json
)
