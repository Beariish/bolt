#pragma once

#if __cplusplus
extern "C" {
#endif

#include "bt_prelude.h"
#include "bt_type.h"
#include "bt_value.h"
#include "bt_op.h"
#include "bt_object.h"
#include "bt_gc.h"
#include "bt_compiler.h"

#include <setjmp.h>
#include <stdint.h>

typedef void* (*bt_Alloc)(size_t size);
typedef void* (*bt_Realloc)(void* ptr, size_t size);
typedef void (*bt_Free)(void* ptr);

typedef char* (*bt_ReadFile)(bt_Context* ctx, const char* path, void** out_handle);
typedef void (*bt_CloseFile)(bt_Context* ctx, const char* path, void*  in_handle);
typedef void (*bt_FreeSource)(bt_Context* ctx, char* source);
typedef void (*bt_Write)(bt_Context* ctx, const char* msg);

/** An entry into the string deduplication table */
typedef struct bt_StringTableEntry {
	uint64_t hash;
	bt_String* string;
} bt_StringTableEntry;

typedef bt_Buffer(bt_StringTableEntry) bt_StringTableBucket;

/** Error category passed to the callback provided in the handlers */
typedef enum {
	BT_ERROR_PARSE,
	BT_ERROR_COMPILE,
	BT_ERROR_RUNTIME,
} bt_ErrorType;

typedef void (*bt_ErrorFunc)(bt_ErrorType type, const char* module, const char* message, uint16_t line, uint16_t col);

// 6b callable ptr, 1b size, 1b user_top
typedef uint64_t bt_StackFrame;

#define BT_MAKE_STACKFRAME(callable, size, user_top) \
	((((uint64_t)(callable)) << 16) | ((size & 0xff) << 8) | (user_top & 0xff))

#define BT_STACKFRAME_GET_CALLABLE(frame) \
	((bt_Callable*)((frame) >> 16))

#define BT_STACKFRAME_GET_SIZE(frame) \
	(((frame) >> 8) & 0xff)

#define BT_STACKFRAME_GET_USER_TOP(frame) \
	((frame) & 0xff)

/** Caller information for a stack frame that crosses the bolt->native barrier */
typedef struct bt_NativeFrame {
	uint8_t argc;
	int8_t return_loc;
} bt_NativeFrame;

/** Module include path template */
typedef struct bt_Path {
	char* spec;
	struct bt_Path* next;
} bt_Path;

/**
 * Set of callback functions expected to be set up for the bolt context to function properly
 * alloc - general purpose allocator
 * free - matching free
 * realloc - matching realloc
 *
 * on_error - callback for whenever the parser, compiler, or runtime encounters an error
 *
 * write - print a string to stdout
 *
 * read_file - open and read an entire file
 * close_file - close a file once done
 * free_source - free the contents of the file
 */
typedef struct bt_Handlers {
	bt_Alloc alloc;
	bt_Free free;
	bt_Realloc realloc;
	bt_ErrorFunc on_error;

	bt_Write write;

	bt_ReadFile read_file;
	bt_CloseFile close_file;
	bt_FreeSource free_source;
} bt_Handlers;

/**
 * A context holds all the relevant information for bolt execution.
 * Each context owns its own garbage collector, all compiled modules, and all allocated objects.
 * This structure is to largely be treated as opaque. 
 */
struct bt_Context {
	bt_CompilerOptions compiler_options;

	bt_Alloc alloc;
	bt_Free free;
	bt_Realloc realloc;
	bt_ErrorFunc on_error;

	bt_Write write;

	bt_ReadFile read_file;
	bt_CloseFile close_file;
	bt_FreeSource free_source;

	bt_Object* root;
	bt_Object* next;
	bt_Object* troots[BT_TEMPROOTS_SIZE];
	uint32_t troot_top;

	bt_GC gc;
	uint32_t n_allocated;

	bt_Path* module_paths;

	bt_StringTableBucket string_table[BT_STRINGTABLE_SIZE];

	struct {
		bt_Type* any;
		bt_Type* null;
		bt_Type* number;
		bt_Type* boolean;
		bt_Type* string;
		bt_Type* array;
		bt_Type* table;
		bt_Type* module;
		bt_Type* type;
	} types;

	struct {
		bt_String* add;
		bt_String* sub;
		bt_String* mul;
		bt_String* div;
		bt_String* lt;
		bt_String* lte;
		bt_String* eq;
		bt_String* neq;
		bt_String* format;
	} meta_names;

	bt_Table* type_registry;
	bt_Table* loaded_modules;
	bt_Table* prelude;
	bt_Table* native_references;

	struct bt_Thread* current_thread;
};

/** A single thread of bolt execution. Threads cannot be executed in parallel on the same context, but can be suspended and swapped */
typedef struct bt_Thread {
	bt_Value stack[BT_STACK_SIZE];
	uint32_t top;

	bt_StackFrame callstack[BT_CALLSTACK_SIZE];
	uint32_t depth;

	bt_NativeFrame native_stack[BT_CALLSTACK_SIZE];
	uint32_t native_depth;

	bt_String* last_error;
	jmp_buf error_loc;

	bt_Context* context;
	bt_Op* ip;
	
	bt_bool should_report;
} bt_Thread;

/** Registers a type to be globally available at compile-time. This is how types like `number` and `bool` are exposed */
BOLT_API void bt_register_type(bt_Context* context, bt_Value name, bt_Type* type);
/** Searches the global type registry for a type with a given name, returns NULL if none are found */
BOLT_API bt_Type* bt_find_type(bt_Context* context, bt_Value name);
/** Registers a value in the context's prelude, which is a list of auto-importable names */
BOLT_API void bt_register_prelude(bt_Context* context, bt_Value name, bt_Type* type, bt_Value value);

/** Register a module with a given name, allowing it to be imported by that alias */
BOLT_API void bt_register_module(bt_Context* context, bt_Value name, bt_Module* module);
/** Search the module registry by name, returning NULL if no module is found. If `suppress_errors` is set, no runtime errors are raised */
BOLT_API bt_Module* bt_find_module(bt_Context* context, bt_Value name, bt_bool suppress_errors);

/** Allocates a new thread with an empty callstack */
BOLT_API bt_Thread* bt_make_thread(bt_Context* context);
/** Resets a thread for reuse without needing to reallocate it */
BOLT_API void bt_reset_thread(bt_Thread* thread);
/** Destroys a thread and frees all references within */
BOLT_API void bt_destroy_thread(bt_Context* context, bt_Thread* thread);

/** Execute the callable, returning whether an error was encountered. This will allocate a temporary thread */
BOLT_API bt_bool bt_execute(bt_Context* context, bt_Callable* callable);
/** Execute the callable on a specific thread, returning whether an error was encountered */
BOLT_API bt_bool bt_execute_on_thread(bt_Context* context, bt_Thread* thread, bt_Callable* callable);
/** Execute the callable on a specific thread, passing along a list of arguments if it takes any. Returns whether an error was encountered */
BOLT_API bt_bool bt_execute_with_args(bt_Context* context, bt_Thread* thread, bt_Callable* callable, bt_Value* args, uint8_t argc);

/** Raise a runtime error and halt execution of the thread, passing along a message. If `ip` is not NULL, it's used to look up debug locations */
BOLT_API void bt_runtime_error(bt_Thread* thread, const char* message, bt_Op* ip);

/** Pushes a value to the top of the thread's value stack */
BOLT_API void bt_push(bt_Thread* thread, bt_Value value);
/** Pops the top value from the thread's value stack */
BOLT_API bt_Value bt_pop(bt_Thread* thread);

/** Creates a closure from values on top of the value stack, in the order of `function, upval..num_upvals` */
BOLT_API bt_Value bt_make_closure(bt_Thread* thread, uint8_t num_upvals);
/** Call a function stored on the value stack, in the order `function, args..argc` */
BOLT_API void bt_call(bt_Thread* thread, uint8_t argc);

/** Append a module import template to the end of the list */
BOLT_API void bt_append_module_path(bt_Context* context, const char* spec);

/** Get the debug source from a callable, or NULL if debug information is disabled */
BOLT_API const char* bt_get_debug_source(bt_Callable* callable);
/** Get the debug token buffer from a callable, or NULL if debug information is disabled */
BOLT_API bt_TokenBuffer* bt_get_debug_tokens(bt_Callable* callable);
/** Get a line from the debug source */
BOLT_API bt_StrSlice bt_get_debug_line(const char* source, uint16_t line);
/** Get the debug location buffer from a callable, which maps opcodes to tokens, or NULL if debug information is disabled */
BOLT_API bt_DebugLocBuffer* bt_get_debug_locs(bt_Callable* callable);
/** Get the token index of the debug information at `ip` */
BOLT_API uint32_t bt_get_debug_index(bt_Callable* callable, bt_Op* ip);

/** Attempt to find an interned string, and if not successful, create one in the deduplication table */
BOLT_API bt_String* bt_get_or_make_interned(bt_Context* ctx, const char* str, uint32_t len);
/** Evict a string from the deduplication table */
BOLT_API void bt_remove_interned(bt_Context* ctx, bt_String* str);

#if __cplusplus
}
#endif