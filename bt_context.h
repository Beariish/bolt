#pragma once

#include "bt_prelude.h"
#include "bt_type.h"
#include "bt_value.h"
#include "bt_op.h"
#include "bt_object.h"
#include "bt_gc.h"

#include <setjmp.h>

typedef void* (*bt_Alloc)(size_t size);
typedef void (*bt_Free)(void* ptr);

#ifndef BT_STACK_SIZE
#define BT_STACK_SIZE 1024
#endif

#ifndef BT_CALLSTACK_SIZE
#define BT_CALLSTACK_SIZE 128
#endif

typedef struct bt_StackFrame {
	uint8_t argc;
	int8_t return_loc;
} bt_StackFrame;

struct bt_Context {
	bt_Alloc alloc;
	bt_Free free;
	bt_BucketedBuffer heap;
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


	bt_Table* type_registry;
	bt_Table* loaded_modules;
	bt_Table* prelude;

	bt_bool is_valid;
};


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
void bt_free_from(bt_Context* context, bt_Bucket* bucket, bt_Object* obj);

#define BT_ALLOCATE(ctx, e_type, c_type) \
	((c_type*)bt_allocate(ctx, sizeof(c_type), (BT_OBJECT_TYPE_##e_type)))

void bt_register_type(bt_Context* context, bt_Value name, bt_Type* type);
bt_Type* bt_find_type(bt_Context* context, bt_Value name);

void bt_register_prelude(bt_Context* context, bt_Value name, bt_Type* type, bt_Value value);
void bt_register_module(bt_Context* context, bt_Value name, bt_Module* module);

bt_Module* bt_find_module(bt_Context* context, bt_Value name);

bt_bool bt_execute(bt_Context* context, bt_Module* module);

void bt_runtime_error(bt_Thread* thread, const char* message);