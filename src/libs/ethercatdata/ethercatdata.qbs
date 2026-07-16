import qbs 1.0

QtcLibrary {
    name: "EtherCATData"

    Depends { name: "Qt"; submodules: ["core"] }

    cpp.defines: base.concat("ETHERCATDATA_LIBRARY")

    files: [
        "devicedescription.h",
        "ethercatdata_global.h",
        "nodeid.cpp",
        "nodeid.h",
        "projectsnapshot.h",
        "scansnapshot.h",
    ]
}
