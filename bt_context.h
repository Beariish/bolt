#pragma once

#include "bt_prelude.h"
#include "bt_type.h"
#include "bt_value.h"
#include "bt_op.h"
#include "bt_object.h"
#include "bt_gc.h"

#include <setjmp.h>

typedef void* (*bt_Alloc)(size_t size);
typedef void* (*bt_Realloc)(void* ptr, size_t size);
typedef void (*bt_Free)(void* ptr);

#ifndef BT_STACK_SIZE
#define BT_STACK_SIZE 1024
#endif

#ifndef BT_CALLSTACK_SIZE
#define BT_CALLSTACK_SIZE 128
#endif

typedef struct bt_StackFrame {
	bt_Callable* callable;
	uint8_t size, argc, user_top;
	int8_t return_loc;
} bt_StackFrame;

struct bt_Context {
	bt_Alloc alloc;
	bt_Free free;
	bt_Realloc realloc;

	bt_Object* root;
	bt_Object* next;
	bt_Object* troots[16];
	uint32_t troot_top;

	bt_GC gc;
	uint32_t n_allocated;

	struct {
		bt_Type* any;
		bt_Type* null;
		bt_Type* number;
		bt_Type* boolean;
		bt_Type* string;
		bt_Type* array;
		bt_Type* table;
		bt_Type* fn;
		bt_Type* shared;
		bt_Type* type;
	} types;

	struct {
		bt_String* add;
		bt_String* sub;
		bt_String* mul;
		bt_String* div;
		bt_String* format;
		bt_String* collect;
	} meta_names;

	bt_Table* type_registry;
	bt_Table* loaded_modules;
	bt_Table* prelude;

	struct bt_Thread* current_thread;

	bt_bool is_valid;
};

void bt_push_root(bt_Context* ctx, bt_Object* root);
void bt_pop_root(bt_Context* ctx);

typedef __declspec(align(64)) struct bt_Thread {
	bt_Value stack[BT_STACK_SIZE];
	uint32_t top;

	bt_StackFrame callstack[BT_CALLSTACK_SIZE];
	uint32_t depth;

	jmp_buf error_loc;

	bt_Context* context;
} bt_Thread;

bt_Object* bt_allocate(bt_Context* context, uint32_t full_size, bt_ObjectType type);
void bt_free(bt_Context* context, bt_Object* obj);

#define BT_ALLOCATE(ctx, e_type, c_type) \
	((c_type*)bt_allocate(ctx, sizeof(c_type), (BT_OBJECT_TYPE_##e_type)))

void bt_register_type(bt_Context* context, bt_Value name, bt_Type* type);
bt_Type* bt_find_type(bt_Context* context, bt_Value name);

void bt_register_prelude(bt_Context* context, bt_Value name, bt_Type* type, bt_Value value);
void bt_register_module(bt_Context* context, bt_Value name, bt_Module* module);

bt_Module* bt_find_module(bt_Context* context, bt_Value name);

bt_bool bt_execute(bt_Context* context, bt_Module* module);

void bt_runtime_error(bt_Thread* thread, const char* message);

void bt_push(bt_Thread* thread, bt_Value value); 
bt_Value bt_pop(bt_Thread* thread);
void bt_call(bt_Thread* thread, uint8_t argc);