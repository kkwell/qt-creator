import qbs 1.0

QtcPlugin {
    name: "EtherCATProductApi"

    Depends { name: "Qt"; submodules: ["network"] }
    Depends { name: "EtherCATData" }
    Depends { name: "Utils" }
    Depends { name: "EtherCATCore" }

    files: [
        "ethercatproductapiconstants.h",
        "ethercatproductapiplugin.cpp",
        "ethercatproductapitr.h",
        "productapicodec.cpp",
        "productapicodec.h",
        "productapiconnectionprovider.cpp",
        "productapiconnectionprovider.h",
        "productapisession.cpp",
        "productapisession.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatproductapitests.cpp",
            "ethercatproductapitests.h",
        ]
    }
}
