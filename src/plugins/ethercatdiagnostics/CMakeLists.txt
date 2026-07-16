add_qtc_plugin(EtherCATDiagnostics
  DEPENDS EtherCATData Qt::Widgets Utils
  PLUGIN_DEPENDS
    Core
    EtherCATCore
    EtherCATProject
    EtherCATWorkbench
  SOURCES
    diagnosticspropertypages.cpp
    diagnosticspropertypages.h
    diagnosticsworkflow.cpp
    diagnosticsworkflow.h
    ethercatdiagnosticsconstants.h
    ethercatdiagnosticsplugin.cpp
    ethercatdiagnosticstr.h
    mockdiagnosticsprovider.cpp
    mockdiagnosticsprovider.h
    mockdiagnosticssampler.cpp
    mockdiagnosticssampler.h
)

extend_qtc_plugin(EtherCATDiagnostics
  CONDITION WITH_TESTS
  SOURCES
    ethercatdiagnosticstests.cpp
    ethercatdiagnosticstests.h
)
