
#ifndef iec61850_shared_EXPORT_H
#define iec61850_shared_EXPORT_H

#ifdef iec61850_shared_BUILT_AS_STATIC
#  define iec61850_shared_EXPORT
#  define IEC61850_SHARED_NO_EXPORT
#else
#  ifndef iec61850_shared_EXPORT
#    ifdef iec61850_shared_EXPORTS
        /* We are building this library */
#      define iec61850_shared_EXPORT __declspec(dllexport)
#    else
        /* We are using this library */
#      define iec61850_shared_EXPORT __declspec(dllimport)
#    endif
#  endif

#  ifndef IEC61850_SHARED_NO_EXPORT
#    define IEC61850_SHARED_NO_EXPORT 
#  endif
#endif

#ifndef IEC61850_SHARED_DEPRECATED
#  define IEC61850_SHARED_DEPRECATED __declspec(deprecated)
#endif

#ifndef IEC61850_SHARED_DEPRECATED_EXPORT
#  define IEC61850_SHARED_DEPRECATED_EXPORT iec61850_shared_EXPORT IEC61850_SHARED_DEPRECATED
#endif

#ifndef IEC61850_SHARED_DEPRECATED_NO_EXPORT
#  define IEC61850_SHARED_DEPRECATED_NO_EXPORT IEC61850_SHARED_NO_EXPORT IEC61850_SHARED_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef IEC61850_SHARED_NO_DEPRECATED
#    define IEC61850_SHARED_NO_DEPRECATED
#  endif
#endif

#endif /* iec61850_shared_EXPORT_H */
