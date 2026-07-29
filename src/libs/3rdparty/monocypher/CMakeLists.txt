add_qtc_library(qtcMonocypher
  STATIC
  SKIP_PCH
  PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    QT_COMPILE_OPTIONS_DISABLE_WARNINGS ON
  PUBLIC_INCLUDES
    .
  SOURCES
    LICENCE.md
    monocypher.c
    monocypher.h
    monocypher-ed25519.c
    monocypher-ed25519.h
  SBOM_ARGS
    SBOM_ENTITY_TYPE THIRD_PARTY_LIBRARY_WITH_FILES
    USE_ATTRIBUTION_FILES
    ATTRIBUTION_FILE_PATHS
      "${QtCreator_SOURCE_DIR}/qt_attributions.json"
    ATTRIBUTION_IDS
      monocypher
)
