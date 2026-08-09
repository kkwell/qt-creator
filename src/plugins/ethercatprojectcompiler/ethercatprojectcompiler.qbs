import qbs
import qbs.FileInfo

Project {
    QtcPlugin {
        name: "EtherCATProjectCompiler"

        Depends { name: "Qt"; submodules: ["core"] }
        Depends { name: "EtherCATData" }
        Depends { name: "qtcMonocypher" }
        Depends { name: "Utils" }
        Depends { name: "Core" }
        Depends { name: "EtherCATCore" }
        Depends {
            name: "ethercatprojectcompiler_fake"
            condition: qtc.withPluginTests
        }

        files: [
            "compileroperationstore.cpp",
            "compileroperationstore.h",
            "compilerinputprovisioningprofile.cpp",
            "compilerinputprovisioningprofile.h",
            "compilerprovisioningprofile.cpp",
            "compilerprovisioningprofile.h",
            "compilerruntimebundleprofile.cpp",
            "compilerruntimebundleprofile.h",
            "durableruntimepackagecompilerpreparationcoordinator.cpp",
            "durableruntimepackagecompilerpreparationcoordinator.h",
            "ethercatprojectcompilerconstants.h",
            "ethercatprojectcompilerplugin.cpp",
            "ethercatprojectcompilertr.h",
            "provisionedruntimepackagecompilerprovider.cpp",
            "provisionedruntimepackagecompilerprovider.h",
            "provisionedruntimepackagecompilerprojectrequestbuilder.cpp",
            "provisionedruntimepackagecompilerprojectrequestbuilder.h",
            "runtimepackagecompilercompilerecoverycodec.cpp",
            "runtimepackagecompilercompilerecoverycodec.h",
            "runtimepackagecompilerpreparationjournal.cpp",
            "runtimepackagecompilerpreparationjournal.h",
        ]

        QtcTestFiles {
            cpp.defines: outer.concat([
                'ETHERCAT_PROJECT_COMPILER_FAKE_EXECUTABLE="'
                    + FileInfo.joinPaths(
                        project.buildDirectory,
                        "ethercatprojectcompiler-test",
                        "ethercatprojectcompiler_fake"
                            + (qbs.targetOS.contains("windows") ? ".exe" : ""))
                    + '"'
            ])
            files: [
                "ethercatprojectcompilertests.cpp",
                "ethercatprojectcompilertests.h",
            ]
        }
    }

    QtApplication {
        name: "ethercatprojectcompiler_fake"
        condition: qtc.withPluginTests
        consoleApplication: true
        install: false

        Depends { name: "qtc" }
        Depends { name: "Qt.core" }

        destinationDirectory: FileInfo.joinPaths(
            project.buildDirectory, "ethercatprojectcompiler-test")
        files: ["test/fakecompiler.cpp"]
    }
}
