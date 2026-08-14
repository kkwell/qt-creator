PlainStaticLibrary {
    name: "qtcMonocypher"
    builtByDefault: false

    Depends { name: "cpp" }

    files: [
        "LICENCE.md",
        "monocypher.c",
        "monocypher.h",
        "monocypher-ed25519.c",
        "monocypher-ed25519.h",
    ]

    cpp.cLanguageVersion: "c99"
    cpp.includePaths: "."
    cpp.warningLevel: "none"

    Export {
        Depends { name: "cpp" }
        cpp.includePaths: exportingProduct.sourceDirectory
    }
}
