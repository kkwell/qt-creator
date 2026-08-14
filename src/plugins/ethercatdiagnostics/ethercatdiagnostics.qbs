import qbs 1.0

QtcPlugin {
    name: "EtherCATDiagnostics"

    Depends { name: "Qt"; submodules: ["widgets"] }
    Depends { name: "EtherCATData" }
    Depends { name: "Utils" }
    Depends { name: "Core" }
    Depends { name: "EtherCATCore" }
    Depends { name: "EtherCATProject" }
    Depends { name: "EtherCATWorkbench" }

    files: [
        "diagnosticspropertypages.cpp",
        "diagnosticspropertypages.h",
        "diagnosticsworkflow.cpp",
        "diagnosticsworkflow.h",
        "ethercatdiagnosticsconstants.h",
        "ethercatdiagnosticsplugin.cpp",
        "ethercatdiagnosticstr.h",
        "mockdiagnosticsprovider.cpp",
        "mockdiagnosticsprovider.h",
        "mockdiagnosticssampler.cpp",
        "mockdiagnosticssampler.h",
    ]

    QtcTestFiles {
        files: [
            "ethercatdiagnosticstests.cpp",
            "ethercatdiagnosticstests.h",
        ]
    }
}
