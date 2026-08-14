#pragma once

#include <qglobal.h>

#if defined(EASYBOARD_LIBRARY)
#  define EASYBOARD_EXPORT Q_DECL_EXPORT
#elif defined(EASYBOARD_STATIC_LIBRARY)
#  define EASYBOARD_EXPORT
#else
#  define EASYBOARD_EXPORT Q_DECL_IMPORT
#endif
