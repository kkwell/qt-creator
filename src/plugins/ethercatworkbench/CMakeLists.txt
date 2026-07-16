add_qtc_plugin(EtherCATWorkbench
  DEPENDS EtherCATData Qt::Widgets Utils
  PLUGIN_DEPENDS Core EtherCATCore EtherCATDevices EtherCATProject
  SOURCES
    builtinpropertypages.cpp
    builtinpropertypages.h
    coeonlinepage.cpp
    coeonlinepage.h
    dcpage.cpp
    dcpage.h
    detailsview.cpp
    detailsview.h
    ethercatworkbenchconstants.h
    ethercatworkbenchplugin.cpp
    ethercatworkbenchtr.h
    processdatapage.cpp
    processdatapage.h
    startuppage.cpp
    startuppage.h
    workbenchcontroller.cpp
    workbenchcontroller.h
    workbenchmode.cpp
    workbenchmode.h
    workbenchnavigation.cpp
    workbenchnavigation.h
    workbenchtreemodel.cpp
    workbenchtreemodel.h
)

extend_qtc_plugin(EtherCATWorkbench
  CONDITION WITH_TESTS
  SOURCES
    ethercatworkbenchtests.cpp
    ethercatworkbenchtests.h
)
