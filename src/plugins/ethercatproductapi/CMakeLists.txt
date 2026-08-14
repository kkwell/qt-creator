add_qtc_plugin(EtherCATProductApi
  DEPENDS EtherCATData Qt::Network Utils
  PLUGIN_DEPENDS EtherCATCore
  SOURCES
    ethercatproductapiconstants.h
    ethercatproductapiplugin.cpp
    ethercatproductapitr.h
    productapicodec.cpp
    productapicodec.h
    productapiconnectionprovider.cpp
    productapiconnectionprovider.h
    productapisession.cpp
    productapisession.h
)

extend_qtc_plugin(EtherCATProductApi
  CONDITION WITH_TESTS
  SOURCES
    ethercatproductapitests.cpp
    ethercatproductapitests.h
)
