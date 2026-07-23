add_qtc_library(EtherCATData
  PUBLIC_DEPENDS Qt::Core
  SOURCES
    controllerconnection.h
    diagnosticssnapshot.h
    devicedescription.h
    ethercatdata_global.h
    nodeid.cpp
    nodeid.h
    offlineconfiguration.cpp
    offlineconfiguration.h
    projectsnapshot.h
    scansnapshot.h
)
