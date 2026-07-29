add_qtc_library(EtherCATData
  PUBLIC_DEPENDS Qt::Core
  SOURCES
    controllerconnection.h
    deviceadapter.h
    diagnosticssnapshot.h
    devicedescription.h
    ethercatdata_global.h
    nodeid.cpp
    nodeid.h
    offlineconfiguration.cpp
    offlineconfiguration.h
    projectsnapshot.h
    runtimeresource.h
    scansnapshot.h
)
