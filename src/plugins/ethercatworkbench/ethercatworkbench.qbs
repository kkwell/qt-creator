import qbs 1.0

QtcPlugin {
    name: "EtherCATWorkbench"

    Depends { name: "Qt"; submodules: ["widgets"] }
    Depends { name: "EtherCATData" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "Debugger" }
    Depends { name: "EtherCATCore" }
    Depends { name: "EtherCATDevices" }
    Depends { name: "EtherCATProject" }
    Depends { name: "EtherCATProjectCompiler" }
    Depends { name: "EtherCATSemanticRuntime" }
    Depends { name: "ProjectExplorer" }

    files: [
        "builtinpropertypages.cpp",
        "builtinpropertypages.h",
        "coeonlinepage.cpp",
        "coeonlinepage.h",
        "communicationpage.cpp",
        "communicationpage.h",
        "dcpage.cpp",
        "dcpage.h",
        "deploymentpage.cpp",
        "deploymentpage.h",
        "deviceparameterspage.cpp",
        "deviceparameterspage.h",
        "detailsview.cpp",
        "detailsview.h",
        "esiconfigurationfactory.cpp",
        "esiconfigurationfactory.h",
        "esidevicegeneralpage.cpp",
        "esidevicegeneralpage.h",
        "esideviceselectiondialog.cpp",
        "esideviceselectiondialog.h",
        "esirepositorypage.cpp",
        "esirepositorypage.h",
        "ethercatworkbenchconstants.h",
        "ethercatworkbenchplugin.cpp",
        "ethercatworkbenchtr.h",
        "ethercatpage.cpp",
        "ethercatpage.h",
        "generalpage.cpp",
        "generalpage.h",
        "processdatapage.cpp",
        "processdatapage.h",
        "runtimepackagecompilerpreparationbridge.cpp",
        "runtimepackagecompilerpreparationbridge.h",
        "semanticcontrolpage.cpp",
        "semanticcontrolpage.h",
        "startuppage.cpp",
        "startuppage.h",
        "workbenchcontroller.cpp",
        "workbenchcontroller.h",
        "workbenchcommandstrip.cpp",
        "workbenchcommandstrip.h",
        "workbenchautomationservice.cpp",
        "workbenchautomationservice.h",
        "workbenchmode.cpp",
        "workbenchmode.h",
        "workbenchnavigation.cpp",
        "workbenchnavigation.h",
        "workbenchstatuswidget.cpp",
        "workbenchstatuswidget.h",
        "workbenchtreemodel.cpp",
        "workbenchtreemodel.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatworkbenchtests.cpp",
            "ethercatworkbenchtests.h",
        ]
    }

    Properties {
        condition: qbs.toolchain.contains("msvc")
        cpp.cxxFlags: "/bigobj"
    }
}
