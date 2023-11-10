#pragma once

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

#ifndef BT_STACK_SIZE
#define BT_STACK_SIZE 1024
#endif

#ifndef BT_CALLSTACK_SIZE
#define BT_CALLSTACK_SIZE 128
#endif

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

/*
typedef struct bt_StackFrame {
	bt_Callable* callable;
	uint8_t size, user_top;
} bt_StackFrame;
*/

typedef struct bt_NativeFrame {
	uint8_t argc;
	int8_t return_loc;
} bt_NativeFrame;

typedef struct bt_Path {
	char* spec;
	struct bt_Path* next;
} bt_Path;

typedef struct bt_Handlers {
	bt_Alloc alloc;
	bt_Free free;
	bt_Realloc realloc;
	bt_ErrorFunc on_error;

	bt_ReadFile read_file;
	bt_CloseFile close_file;
	bt_FreeSource free_source;
} bt_Handlers;

struct bt_Context {
	bt_CompilerOptions compiler_options;

	bt_Alloc alloc;
	bt_Free free;
	bt_Realloc realloc;
	bt_ErrorFunc on_error;

	bt_ReadFile read_file;
	bt_CloseFile close_file;
	bt_FreeSource free_source;

	bt_Object* root;
	bt_Object* next;
	bt_Object* troots[16];
	uint32_t troot_top;

	bt_GC gc;
	uint32_t n_allocated;

	bt_Path* module_paths;

	struct {
		bt_Type* any;
		bt_Type* null;
		bt_Type* number;
		bt_Type* boolean;
		bt_Type* string;
		bt_Type* array;
		bt_Type* table;
		bt_Type* fn;
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
		bt_String* collect;
	} meta_names;

	bt_Table* type_registry;
	bt_Table* loaded_modules;
	bt_Table* prelude;
	bt_Table* native_references;

	struct bt_Thread* current_thread;

	bt_bool is_valid;
};

BOLT_API void bt_push_root(bt_Context* ctx, bt_Object* root);
BOLT_API void bt_pop_root(bt_Context* ctx);

BOLT_API uint32_t bt_add_ref(bt_Context* ctx, bt_Object* obj);
BOLT_API uint32_t bt_remove_ref(bt_Context* ctx, bt_Object* obj);

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
	
	bt_bool should_report;
} bt_Thread;

BOLT_API bt_Object* bt_allocate(bt_Context* context, uint32_t full_size, bt_ObjectType type);
BOLT_API void bt_free(bt_Context* context, bt_Object* obj);

#define BT_ALLOCATE(ctx, e_type, c_type) \
	((c_type*)bt_allocate(ctx, sizeof(c_type), (BT_OBJECT_TYPE_##e_type)))

#define BT_ALLOCATE_INLINE_STORAGE(ctx, e_type, c_type, storage) \
	((c_type*)bt_allocate(ctx, sizeof(c_type) + storage, (BT_OBJECT_TYPE_##e_type)))

BOLT_API void bt_register_type(bt_Context* context, bt_Value name, bt_Type* type);
BOLT_API bt_Type* bt_find_type(bt_Context* context, bt_Value name);

BOLT_API void bt_register_prelude(bt_Context* context, bt_Value name, bt_Type* type, bt_Value value);
BOLT_API void bt_register_module(bt_Context* context, bt_Value name, bt_Module* module);

BOLT_API bt_Module* bt_find_module(bt_Context* context, bt_Value name);

BOLT_API bt_Thread* bt_make_thread(bt_Context* context);
BOLT_API void bt_destroy_thread(bt_Context* context, bt_Thread* thread);

BOLT_API bt_bool bt_execute(bt_Context* context, bt_Callable* callable);
BOLT_API bt_bool bt_execute_on_thread(bt_Context* context, bt_Thread* thread, bt_Callable* callable);
BOLT_API bt_bool bt_execute_with_args(bt_Context* context, bt_Thread* thread, bt_Callable* callable, bt_Value* args, uint8_t argc);

BOLT_API void bt_runtime_error(bt_Thread* thread, const char* message, bt_Op* ip);

BOLT_API void bt_push(bt_Thread* thread, bt_Value value); 
BOLT_API bt_Value bt_pop(bt_Thread* thread);

BOLT_API bt_Value bt_make_closure(bt_Thread* thread, uint8_t num_upvals);

BOLT_API void bt_call(bt_Thread* thread, uint8_t argc);

BOLT_API void bt_append_module_path(bt_Context* context, const char* spec);

BOLT_API const char* bt_get_debug_source(bt_Callable* callable);
BOLT_API bt_TokenBuffer* bt_get_debug_tokens(bt_Callable* callable);
BOLT_API bt_StrSlice bt_get_debug_line(const char* source, uint16_t line);
BOLT_API bt_DebugLocBuffer* bt_get_debug_locs(bt_Callable* callable); 
BOLT_API uint32_t bt_get_debug_index(bt_Callable* callable, bt_Op* ip);