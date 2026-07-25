import qbs 1.0

QtcPlugin {
    name: "EtherCATAutomationGateway"

    Depends { name: "Qt"; submodules: ["core", "httpserver", "network"] }
    Depends { name: "EtherCATData" }
    Depends { name: "ExtensionSystem" }
    Depends { name: "McpServerLib" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }
    Depends { name: "EtherCATWorkbench" }

    files: [
        "automationdispatcher.cpp",
        "automationdispatcher.h",
        "controller-tools-v1-contracts.qrc",
        "ethercatautomationgatewayconstants.h",
        "ethercatautomationgatewayplugin.cpp",
        "gatewayserver.cpp",
        "gatewayserver.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatautomationgatewaytests.cpp",
            "ethercatautomationgatewaytests.h",
        ]
    }
}
