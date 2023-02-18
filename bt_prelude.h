#pragma once

#include <stdint.h>

#define BOLT_VERSION "0.0.1"

//#define BOLT_PRINT_DEBUG

#ifdef _NDEBUG
#undef BOLT_DEBUG
#else
#define BOLT_DEBUG 1
#endif

#define BT_TRUE 1
#define BT_FALSE 0

typedef uint8_t bt_bool;
typedef double bt_number;

typedef struct {
	const char* source;
	uint16_t length;
} bt_StrSlice;

bt_bool bt_strslice_compare(bt_StrSlice a, bt_StrSlice b);

typedef struct bt_Context bt_Context;
typedef struct bt_Thread bt_Thread; 
typedef struct bt_StackFrame bt_StackFrame;