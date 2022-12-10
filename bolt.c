#include "bolt.h"

#include "bt_object.h"
#include <assert.h>
#include <stdio.h>

static bt_Type* make_primitive_type(bt_Context* ctx, const char* name, bt_TypeSatisfier satisfier)
{
	return bt_make_type(ctx, name, satisfier, BT_TYPE_CATEGORY_PRIMITIVE, BT_FALSE);
}

void bt_open(bt_Context* context, bt_Alloc allocator, bt_Free free)
{
	context->alloc = allocator;
	context->free = free;

	context->heap = BT_BUCKETED_BUFFER_NEW(context, 256, bt_Object*);

	context->types.number = make_primitive_type(context, "number", bt_type_satisfier_same);
	context->types.boolean = make_primitive_type(context, "bool", bt_type_satisfier_same);
	context->types.string = make_primitive_type(context, "string", bt_type_satisfier_same);
	context->types.table = make_primitive_type(context, "table", bt_type_satisfier_same);

	context->types.any = make_primitive_type(context, "any", bt_type_satisfier_any);
	context->types.any->is_optional = BT_TRUE;

	context->types.null = make_primitive_type(context, "null", bt_type_satisfier_null);
	context->types.null->is_optional = BT_TRUE;

	context->types.array = make_primitive_type(context, "array", bt_type_satisfier_array);
	context->types.array->category = BT_TYPE_CATEGORY_ARRAY;
	context->types.array->as.array.inner = context->types.any;

	context->type_registry = bt_make_table(context, 16);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "number")), context->types.number);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "bool")), context->types.boolean);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "string")), context->types.string);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "table")), context->types.table);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "any")), context->types.any);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "null")), context->types.null);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "array")), context->types.array);

	context->loaded_modules = bt_make_table(context, 1);
	context->prelude = bt_make_table(context, 16);

	context->is_valid = BT_TRUE;
}

bt_Object* bt_allocate(bt_Context* context, uint32_t full_size, bt_ObjectType type)
{
	bt_Object* obj = context->alloc(full_size);
	obj->type = type;

	obj->heap_idx = bt_bucketed_buffer_insert(context, &context->heap, &obj);

	return obj;
}

void bt_register_type(bt_Context* context, bt_Value name, bt_Type* type)
{
	bt_table_set(context, context->type_registry, name, BT_VALUE_OBJECT(type));
}

bt_Type* bt_find_type(bt_Context* context, bt_Value name)
{
	return (bt_Type*)BT_AS_OBJECT(bt_table_get(context->type_registry, name));
}

void bt_register_prelude(bt_Context* context, bt_Value name, bt_Type* type, bt_Value value)
{
	bt_ModuleImport* new_import = BT_ALLOCATE(context, IMPORT, bt_ModuleImport);
	new_import->name = BT_AS_STRING(name);
	new_import->type = type;
	new_import->value = value;

	bt_table_set(context, context->prelude, name, BT_VALUE_OBJECT(new_import));
}

void bt_register_module(bt_Context* context, bt_Value name, bt_Module* module)
{
	bt_table_set(context, context->loaded_modules, name, BT_VALUE_OBJECT(module));
}

bt_Module* bt_find_module(bt_Context* context, bt_Value name)
{
	// TODO: Attempt to load module if not found!
	return BT_AS_OBJECT(bt_table_get(context->loaded_modules, name));
}

static void bt_call(bt_Context* context, bt_Thread* thread, bt_Op* ip, bt_Value* constants, uint8_t stack_size, int8_t return_loc);

bt_bool bt_execute(bt_Context* context, bt_Module* module)
{
	bt_Thread thread;
	thread.depth = 0;
	thread.top = 0;

	thread.callstack[thread.depth++] = module;

	int32_t result = setjmp(thread.error_loc);

	if(result == 0) bt_call(context, &thread, module->instructions.data, module->constants.data, module->stack_size, 0);
	else {
		return BT_FALSE;
	}

	bt_number str = BT_AS_NUMBER(thread.stack[0]);
	printf("Module returned: '%f'\n", str);

	return BT_TRUE;
}

void bt_runtime_error(bt_Thread* thread, const char* message)
{
	printf("BOLT ERROR: %s\n", message);
	longjmp(thread->error_loc, 1);
}

static __forceinline void bt_add(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) + BT_AS_NUMBER(rhs));
		return;
	}

	if (BT_TYPEOF(lhs) != BT_TYPEOF(rhs)) {
		bt_runtime_error(thread, "Cannot add separate types!");
	}

	bt_runtime_error(thread, "Unable to add values of type <TODO>!");
}

static __forceinline void bt_neg(bt_Thread* thread, bt_Value* result, bt_Value rhs)
{
	if (BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(-BT_AS_NUMBER(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot negate non-number value!");
}

static __forceinline void bt_sub(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) - BT_AS_NUMBER(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot subtract non-number value!");
}
static __forceinline void bt_mul(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) * BT_AS_NUMBER(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot subtract non-number value!");
}

static __forceinline void bt_div(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) / BT_AS_NUMBER(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot subtract non-number value!");
}

static void bt_call(bt_Context* context, bt_Thread* thread, bt_Op* ip, bt_Value* constants, uint8_t stack_size, int8_t return_loc)
{
	bt_Op op;
	bt_Value* stack = thread->stack + thread->top;
#define NEXT goto dispatch;
#define RETURN goto end;
dispatch:
	op = *ip++;
	switch (op.op) {
	case BT_OP_LOAD:        stack[op.a] = constants[op.b];                       NEXT;
	case BT_OP_LOAD_SMALL:  stack[op.a] = BT_VALUE_NUMBER(op.ibc);               NEXT;
	case BT_OP_LOAD_NULL:   stack[op.a] = BT_VALUE_NULL;                         NEXT;
	case BT_OP_LOAD_BOOL:   stack[op.a] = op.b ? BT_VALUE_TRUE : BT_VALUE_FALSE; NEXT;
	case BT_OP_LOAD_IMPORT: 
		stack[op.a] =
			(*(bt_ModuleImport**)bt_buffer_at(&thread->callstack[0]->module.imports, op.ubc))->value;
		NEXT;
	
	case BT_OP_MOVE: stack[op.a] = stack[op.b]; NEXT;
	
	case BT_OP_NEG: bt_neg(thread, stack + op.a, stack[op.b]);              NEXT;
	case BT_OP_ADD: bt_add(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;
	case BT_OP_SUB: bt_sub(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;
	case BT_OP_MUL: bt_mul(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;
	case BT_OP_DIV: bt_div(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;

	case BT_OP_LOAD_IDX: stack[op.a] = bt_table_get(BT_AS_OBJECT(stack[op.b]), stack[op.c]); NEXT;
	
	case BT_OP_EXISTS:   stack[op.a] = stack[op.b] == BT_VALUE_NULL ? BT_VALUE_FALSE : BT_VALUE_TRUE; NEXT;
	case BT_OP_COALESCE: stack[op.a] = stack[op.b] == BT_VALUE_NULL ? stack[op.c]    : stack[op.b];   NEXT;
	
	case BT_OP_CALL: {
		uint16_t old_top = thread->top;
		bt_Fn* callable = (bt_Fn*)BT_AS_OBJECT(stack[op.b]);
		thread->top += op.b + 1;

		thread->callstack[thread->depth++] = callable;
		bt_call(context, thread, callable->instructions.data, callable->constants.data, callable->stack_size, op.a - thread->top);
		thread->top = old_top;
	} NEXT;

	case BT_OP_RETURN: stack[return_loc] = stack[op.a]; RETURN;
	case BT_OP_END: RETURN;
	case BT_OP_HALT: assert(0 && "This should never happen, internal compiler error.");
	default: __debugbreak();
	}

end:
	thread->depth--;
}

/*
import sqrt from "math"

type Vec2 = { x: number, y: number }

method Vec2.__new(x: number, y: number) {
	this.x = x
	this.y = y
}

method Vec2.__add(rhs: Vec2): Vec2 {
	return new Vec2(
		this.x + rhs.x,
		this.y + rhs.y)
}

method Vec2.length: number {
	return sqrt(this.x * this.x + this.y * this.y)
}

let a = new Vec2(10, 20)
let b = new Vec2(4, 5)

print((a + b).length()) 

*/