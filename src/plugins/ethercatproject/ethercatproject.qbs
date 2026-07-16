import qbs 1.0

QtcPlugin {
    name: "EtherCATProject"

    Depends { name: "Qt"; submodules: ["widgets"] }
    Depends { name: "EtherCATData" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }
    Depends { name: "ProjectExplorer" }

    files: [
        "ethercatproject.cpp",
        "ethercatproject.h",
        "ethercatprojectconstants.h",
        "ethercatprojectdocument.cpp",
        "ethercatprojectdocument.h",
        "ethercatprojectformat.cpp",
        "ethercatprojectformat.h",
        "ethercatprojectplugin.cpp",
        "ethercatprojecttr.h",
        "ethercatprojectwizard.cpp",
        "ethercatprojectwizard.h",
        "projectserviceimpl.cpp",
        "projectserviceimpl.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatprojecttests.cpp",
            "ethercatprojecttests.h",
        ]
    }
}
