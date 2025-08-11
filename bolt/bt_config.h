#pragma once

// This header exists in order to configure some parts of bolts functionality, it is included in the relevant places
// Whether or not these are enabled shouldn't intergere with ABI boundary, but behvaiour may vary between compilers

// Enabling this dots a lot of the bolt internals with assertions, useful to hunt down unwanted behavour
//#define BT_DEBUG

// Use explicit butmasking in order to utilize otherwise-zero'd bits inside the GC next pointer
// This reduces the size of ALL BOLT OBJECTS by 8 bytes, but does make debugging more challenging
// as it's impossible to really inspect the GC state
#define BOLT_USE_MASKED_GC_HEADER

// This replaces the union struct normally used to represent bolt operations with bitmasked integers instead
// On MSVC specifically, this gives measurable speedup. Other platforms may vary.
// This does also make debugging more tedious though.
#define BOLT_BITMASK_OP

// Enables more debug printing throughout bolt execution, dumping things like token stream, ast state, and 
// compiled bytecode to the console.
//#define BOLT_PRINT_DEBUG

// Inline threading allows for bolt to make indirect jumps from each instruction to each next instruction
// In theory, this increases performance due to branch prediction, but costs more code size.
// Will increase perf in most scenarios, but not all.
// Also takes significantly longer to compile.
#define BOLT_USE_INLINE_THREADING

// Allows for the use of the cstdlib to set up some reasonable default handlers for memory allocation
#ifndef __APPLE__
#define BOLT_ALLOW_MALLOC
#endif

// Allows for the use of the cstdlib to set up a default error handler
#define BOLT_ALLOW_PRINTF

// Allows for the use of the cstdlib to set up default module loaders
#define BOLT_ALLOW_FOPEN

// Builds bolt as a shared library as opposed to statically linking
// Make sure BOLT_EXPORT_SHARED is defined when building the library
//#define BOLT_SHARED_LIBRARY

// Builds bolt as a static library, meaning inlined functions get their 
// own translation units
#define BOLT_STATIC_LIBRARY

// Defines the size of pooled allocations made in the parser for speed
// Larger values may have a minor impact on parsing performance, but
// comes at the cost of larger memory overhead while compiling
#ifndef BT_AST_NODE_POOL_SIZE
#define BT_AST_NODE_POOL_SIZE 64
#endif

// The size of the value stack allocated by a bolt thread, measured in sizeof(bt_Value)'s (typically 8 bytes)
#ifndef BT_STACK_SIZE
#define BT_STACK_SIZE 1024
#endif

// The size of the callstack allocated by a bolt thread, exceeding this will immediately halt execution
#ifndef BT_CALLSTACK_SIZE
#define BT_CALLSTACK_SIZE 128
#endif

// The number of root entries in the string deduplication table
// 126 is tuned to cover the standard ascii charset 
#ifndef BT_STRINGTABLE_SIZE
#define BT_STRINGTABLE_SIZE 126
#endif

// The maximum length of a string that is considered for interning
// Longer values may compromise runtime performance due to excess hashing
#ifndef BT_STRINGTABLE_MAX_LEN
#define BT_STRINGTABLE_MAX_LEN 24
#endif

// The size of the temporary root stack kept by the bolt context
// to stop temporary objects from being collected while in use 
#ifndef BT_TEMPROOTS_SIZE
#define BT_TEMPROOTS_SIZE 16
#endif

// The maximum path length for a module path specification
#ifndef BT_MODULE_PATH_SIZE
#define BT_MODULE_PATH_SIZE 512
#endif