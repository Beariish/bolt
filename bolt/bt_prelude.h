#pragma once

#if __cplusplus
extern "C" {
#endif

#include "bt_config.h"
#include <stdint.h>
#include <stddef.h>

#define BOLT_VERSION_MAJOR 0
#define BOLT_VERSION_MINOR 1
#define BOLT_VERSION_REVISION 4

#define BOLT_STR(x) #x
#define BOLT_XSTR(x) BOLT_STR(x)
#define BOLT_VERSION BOLT_XSTR(BOLT_VERSION_MAJOR) "." BOLT_XSTR(BOLT_VERSION_MINOR) "." BOLT_XSTR(BOLT_VERSION_REVISION)

#ifdef _NDEBUG
#undef BOLT_DEBUG
#else
#define BOLT_DEBUG 1
#endif

#define BT_TRUE 1
#define BT_FALSE 0

#ifdef _MSC_VER
#define BT_FORCE_INLINE __forceinline
#define BT_NO_INLINE __declspec(noinline)
#define BT_ASSUME(x) __assume(x)
#else
#define BT_FORCE_INLINE __attribute__((always_inline)) inline
#define BT_NO_INLINE
#if __has_builtin(__builtin_assume)
  #define BT_ASSUME(x) __builtin_assume(x)
#else
  #define BT_ASSUME(x) do { if (!(x)) __builtin_unreachable(); } while(0)
#endif
#endif

#ifdef BOLT_SHARED_LIBRARY
	#ifdef BOLT_EXPORT_SHARED
		#ifdef _MSC_VER
			#define BOLT_API __declspec(dllexport)
		#else
			#define BOLT_API __attribute__((dllexport))
		#endif
	#else
		#ifdef _MSC_VER
			#define BOLT_API __declspec(dllimport)
		#else
			#define BOLT_API __attribute__((dllimport))
		#endif
	#endif
#else
	#define BOLT_API
#endif

#ifndef BT_PREFETCH_READ_MODERATE
    #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        #include <xmmintrin.h>
        #define BT_PREFETCH_READ_MODERATE(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T2)
    #elif (defined(__GNUC__) || defined(__clang__)) && __has_builtin(__builtin_prefetch)
        #define BT_PREFETCH_READ_MODERATE(addr) __builtin_prefetch((addr), 0, 2)
    #else
        #define BT_PREFETCH_READ_MODERATE(addr) ((void)(addr))
    #endif
#endif

typedef uint8_t bt_bool;
typedef double bt_number;

/** Source + length string slice, assumes no null termination */
typedef struct {
	const char* source;
	uint16_t length;
} bt_StrSlice;

/** Compare whether two string slinces are equal */
BOLT_API bt_bool bt_strslice_compare(bt_StrSlice a, bt_StrSlice b);

typedef struct bt_Context bt_Context;
typedef struct bt_Thread bt_Thread; 
typedef struct bt_Handlers bt_Handlers;

#if __cplusplus
}
#endif