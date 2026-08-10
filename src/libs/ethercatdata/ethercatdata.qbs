import qbs 1.0

QtcLibrary {
    name: "EtherCATData"

    Depends { name: "Qt"; submodules: ["core"] }

    cpp.defines: base.concat("ETHERCATDATA_LIBRARY")

    files: [
        "controllerconnection.h",
        "deviceadapter.h",
        "deviceadapterselection.h",
        "deviceparameters.h",
        "diagnosticssnapshot.h",
        "devicedescription.h",
        "engineeringvalue.h",
        "ethercatdata_global.h",
        "manualcontrol.h",
        "nodeid.cpp",
        "nodeid.h",
        "offlineconfiguration.cpp",
        "offlineconfiguration.h",
        "projectsnapshot.h",
        "runtimepackageactivation.cpp",
        "runtimepackageactivation.h",
        "runtimepackagecompiler.cpp",
        "runtimepackagecompiler.h",
        "runtimeoutputtransaction.h",
        "runtimeresource.h",
        "scansnapshot.h",
        "semanticmappingattestation.h",
        "semanticids.h",
        "semanticruntime.h",
    ]
}
