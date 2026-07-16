// Copyright (C) 2026 Kvell

#pragma once

#include <qglobal.h>

#if defined(ETHERCATCORE_LIBRARY)
#define ETHERCATCORE_EXPORT Q_DECL_EXPORT
#elif defined(ETHERCATCORE_STATIC_LIBRARY)
#define ETHERCATCORE_EXPORT
#else
#define ETHERCATCORE_EXPORT Q_DECL_IMPORT
#endif
