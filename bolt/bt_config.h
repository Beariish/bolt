#pragma once

// This header exists in order to configure some parts of bolts functionality, it is included in the relevant places
// Whether or not these are enabled shouldn't intergere with ABI boundary, but behvaiour may vary between compilers

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
#define BOLT_ALLOW_MALLOC

// Allows for the use of the cstdlib to set up a default error handler
#define BOLT_ALLOW_PRINTF

// Allows for the use of the cstdlib to set up default module loaders
#define BOLT_ALLOW_FOPEN

// Builds bolt as a shared library as opposed to statically linking
// Make sure BOLT_EXPORT_SHARED is defined when building the library
//#define BOLT_SHARED_LIBRARY