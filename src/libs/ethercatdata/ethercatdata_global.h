// Copyright (C) 2026 Kvell

#pragma once

#include <qglobal.h>

#if defined(ETHERCATDATA_LIBRARY)
#define ETHERCATDATA_EXPORT Q_DECL_EXPORT
#elif defined(ETHERCATDATA_STATIC_LIBRARY)
#define ETHERCATDATA_EXPORT
#else
#define ETHERCATDATA_EXPORT Q_DECL_IMPORT
#endif
