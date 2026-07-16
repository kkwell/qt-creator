import qbs 1.0

QtcLibrary {
    name: "EtherCATData"

    Depends { name: "Qt"; submodules: ["core"] }

    cpp.defines: base.concat("ETHERCATDATA_LIBRARY")

    files: [
        "ethercatdata_global.h",
        "nodeid.cpp",
        "nodeid.h",
    ]
}
