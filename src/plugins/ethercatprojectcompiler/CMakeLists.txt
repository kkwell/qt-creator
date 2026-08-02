add_qtc_plugin(EtherCATProjectCompiler
  DEPENDS EtherCATData Qt::Core Utils qtcMonocypher
  PLUGIN_DEPENDS Core EtherCATCore
  SOURCES
    compileroperationstore.cpp
    compileroperationstore.h
    compilerinputprovisioningprofile.cpp
    compilerinputprovisioningprofile.h
    compilerprovisioningprofile.cpp
    compilerprovisioningprofile.h
    durableruntimepackagecompilerpreparationcoordinator.cpp
    durableruntimepackagecompilerpreparationcoordinator.h
    ethercatprojectcompilerconstants.h
    ethercatprojectcompilerplugin.cpp
    ethercatprojectcompilertr.h
    provisionedruntimepackagecompilerprovider.cpp
    provisionedruntimepackagecompilerprovider.h
    provisionedruntimepackagecompilerprojectrequestbuilder.cpp
    provisionedruntimepackagecompilerprojectrequestbuilder.h
    runtimepackagecompilercompilerecoverycodec.cpp
    runtimepackagecompilercompilerecoverycodec.h
    runtimepackagecompilerpreparationjournal.cpp
    runtimepackagecompilerpreparationjournal.h
)

if (WITH_TESTS AND TARGET EtherCATProjectCompiler)
  add_executable(ethercatprojectcompiler_fake
    test/fakecompiler.cpp
  )
  target_link_libraries(ethercatprojectcompiler_fake PRIVATE Qt::Core)
  add_dependencies(EtherCATProjectCompiler ethercatprojectcompiler_fake)
  target_compile_definitions(EtherCATProjectCompiler PRIVATE
    ETHERCAT_PROJECT_COMPILER_FAKE_EXECUTABLE="$<TARGET_FILE:ethercatprojectcompiler_fake>"
  )
endif()

extend_qtc_plugin(EtherCATProjectCompiler
  CONDITION WITH_TESTS
  SOURCES
    ethercatprojectcompilertests.cpp
    ethercatprojectcompilertests.h
)
