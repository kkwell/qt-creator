import qbs 1.0

QtcPlugin {
    name: "EtherCATAutomationGateway"

    Depends { name: "Qt"; submodules: ["core", "httpserver", "network", "widgets"] }
    Depends { name: "EtherCATData" }
    Depends { name: "ExtensionSystem" }
    Depends { name: "McpServerLib" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }
    Depends { name: "EtherCATSemanticRuntime" }
    Depends { name: "EtherCATWorkbench" }
    Depends { name: "ProjectExplorer" }

    files: [
        "automationdispatcher.cpp",
        "automationdispatcher.h",
        "controller-tools-v1-contracts.qrc",
        "ethercatautomationgatewayconstants.h",
        "ethercatautomationgatewayplugin.cpp",
        "ethercatautomationgatewaytr.h",
        "gatewayruntime.cpp",
        "gatewayruntime.h",
        "gatewayserver.cpp",
        "gatewayserver.h",
        "gatewaysettingspage.cpp",
        "gatewaysettingspage.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatautomationgatewaytests.cpp",
            "ethercatautomationgatewaytests.h",
            "tests/controller_tools_v1_probe.py",
        ]
    }
}
