import qbs 1.0

QtcLibrary {
    name: "EtherCATData"

    Depends { name: "Qt"; submodules: ["core"] }

    cpp.defines: base.concat("ETHERCATDATA_LIBRARY")

    files: [
        "controllerconnection.h",
        "deviceadapter.h",
        "deviceadapterselection.h",
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
        "runtimeoutputtransaction.h",
        "runtimeresource.h",
        "scansnapshot.h",
        "semanticids.h",
        "semanticruntime.h",
    ]
}
