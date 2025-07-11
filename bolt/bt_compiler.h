#pragma once

#if __cplusplus
extern "C" {
#endif
	
#include "bt_prelude.h"
#include "bt_parser.h"

/**
 * A list of options the compiler takes into consideration when generating bytecode.
 * By default, all of these will be turned on. 
 */
typedef struct bt_CompilerOptions {
	/** Should we generate debug info? If turned off, runtime errors will have no location information, but memory footprint will be smaller */
	bt_bool generate_debug_info;
	/** If enabled, the compiler will generate accelerated opcode variants for numeric arithmetic whenever type information allows */
	bt_bool accelerate_arithmetic;
	/** If enabled, the compiler will "hoist" prototype functions from their surrounding tableshapes whenever safe, avoiding the runtime lookup */
	bt_bool allow_method_hoisting;
	/** If enabled, the compiler will generate faster versions of table indexing opcodes whenever the slot can be predicted */
	bt_bool predict_hash_slots;
	/** If enabled, the compiler will generate accelerated opcodes for array indexing whenever the type information allows */
	bt_bool typed_array_subscript;
} bt_CompilerOptions;

typedef struct bt_Compiler {
	bt_CompilerOptions options;

	bt_AstNode* debug_stack[128];
	uint32_t debug_top;

	bt_Context* context;
	bt_Parser* input;

	bt_bool has_errored;
} bt_Compiler;

/** Creates a compiler around `parser` in a valid default state, taking `options` into account */
BOLT_API bt_Compiler bt_open_compiler(bt_Parser* parser, bt_CompilerOptions options);

/** Closes a compiler and frees all owned memory */
BOLT_API void bt_close_compiler(bt_Compiler* compiler);

/**
 * Compiles the AST provided by the parser passed to `compiler` during creation, attempting to produce a module.
 * Errors are reported to the context throughout compilation, and if any are encountered, compilation will halt.
 *
 * Returns a valid module if compilation succeeds, else NULL.
 */
BOLT_API bt_Module* bt_compile(bt_Compiler* compiler);

#if __cplusplus
}
#endif