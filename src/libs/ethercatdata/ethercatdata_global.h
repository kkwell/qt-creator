// Copyright (C) 2026 Kvell

#pragma once

#include <qglobal.h>

#if defined(ETHERCATDATA_LIBRARY)
#define ETHERCATDATA_EXPORT Q_DECL_EXPORT
#elif defined(ETHERCATDATA_STATIC_LIBRARY)
#define ETHERCATDATA_EXPORT
#elif defined(Q_CC_MSVC)
// Keep inline special members of value types local to MSVC consumers. Importing the entire
// aggregate makes MSVC expect DLL symbols that are never emitted when the library does not use
// those implicitly generated members itself.
#define ETHERCATDATA_EXPORT
#else
#define ETHERCATDATA_EXPORT Q_DECL_IMPORT
#endif
