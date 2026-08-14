add_qtc_plugin(EtherCATProject
  DEPENDS EtherCATData Qt::Widgets Utils
  PLUGIN_DEPENDS Core EtherCATCore ProjectExplorer
  SOURCES
    ethercatproject.cpp
    ethercatproject.h
    ethercatprojectconstants.h
    ethercatprojectdocument.cpp
    ethercatprojectdocument.h
    ethercatprojectformat.cpp
    ethercatprojectformat.h
    ethercatprojectplugin.cpp
    ethercatprojecttr.h
    ethercatprojectwizard.cpp
    ethercatprojectwizard.h
    projectserviceimpl.cpp
    projectserviceimpl.h
)

extend_qtc_plugin(EtherCATProject
  CONDITION WITH_TESTS
  SOURCES
    ethercatprojecttests.cpp
    ethercatprojecttests.h
)
