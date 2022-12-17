#include "bolt.h"

#include "bt_object.h"
#include <assert.h>
#include <stdio.h>
#include <memory.h>

#include "bt_tokenizer.h"
#include "bt_parser.h"
#include "bt_compiler.h"
#include "bt_debug.h"

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

bt_bool bt_run(bt_Context* context, const char* source)
{
	bt_Module* mod = bt_compile_module(context, source);
	if (!mod) return BT_FALSE;
	return bt_execute(context, mod);
}

bt_Module* bt_compile_module(bt_Context* context, const char* source)
{
	printf("%s\n", source);
	printf("-----------------------------------------------------\n");


	bt_Tokenizer* tok = context->alloc(sizeof(bt_Tokenizer));
	*tok = bt_open_tokenizer(context);
	bt_tokenizer_set_source(tok, source);

	bt_Parser* parser = context->alloc(sizeof(bt_Parser));
	*parser = bt_open_parser(tok);
	if (bt_parse(parser) == BT_FALSE) {
		bt_close_parser(parser);
		bt_close_tokenizer(tok);
		context->free(parser);
		context->free(tok);
		return NULL;
	}
	
	printf("-----------------------------------------------------\n");

	bt_Compiler* compiler = context->alloc(sizeof(bt_Compiler));
	*compiler = bt_open_compiler(parser);
	bt_Module* result = bt_compile(compiler);
	if (result == 0) {
		bt_close_compiler(compiler);
		bt_close_parser(parser);
		bt_close_tokenizer(tok);
		context->free(compiler);
		context->free(parser);
		context->free(tok);
		return NULL;
	}

	bt_debug_print_module(context, result);
	printf("-----------------------------------------------------\n");

	bt_close_compiler(compiler);
	bt_close_parser(parser);
	bt_close_tokenizer(tok);

	context->free(compiler);
	context->free(parser);
	context->free(tok);

	return result;
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
	// TODO: resolve module name with path
	bt_Module* mod = BT_AS_OBJECT(bt_table_get(context->loaded_modules, name));
	if (mod == 0) {
		bt_String* to_load = BT_AS_STRING(name);
		
		char* name_as_bolt_file = context->alloc(to_load->len + 5 + 1);
		memcpy(name_as_bolt_file, to_load->str, to_load->len);
		memcpy(name_as_bolt_file + to_load->len, ".bolt", 5);
		name_as_bolt_file[to_load->len + 5] = 0;

		FILE* source;
		fopen_s(&source, name_as_bolt_file, "rb");
		context->free(name_as_bolt_file);
		
		if (source == 0) {
			assert(0 && "Cannot find module file!");
			return NULL;
		}

		fseek(source, 0, SEEK_END);
		uint32_t len = ftell(source);
		fseek(source, 0, SEEK_SET);

		char* code = context->alloc(len + 1);
		fread(code, 1, len, source);
		fclose(source);
		code[len] = 0;

		bt_Module* new_mod = bt_compile_module(context, code);
		context->free(code);

		if (new_mod) {
			bt_execute(context, new_mod);
			bt_register_module(context, name, new_mod);
			return new_mod;
		}
		else {
			return NULL;
		}
	}

	return mod;
}

static void bt_call(bt_Context* context, bt_Thread* thread, bt_Op* ip, bt_Value* constants, uint8_t stack_size, int8_t return_loc);

bt_bool bt_execute(bt_Context* context, bt_Module* module)
{
	bt_Thread thread;
	thread.depth = 0;
	thread.top = 0;
	thread.context = context;

	thread.callstack[thread.depth].callable = module;
	thread.callstack[thread.depth].return_loc = 0;
	thread.callstack[thread.depth].argc = 0;
	thread.callstack[thread.depth].module = module;
	thread.depth++;

	int32_t result = setjmp(thread.error_loc);

	if(result == 0) bt_call(context, &thread, module->instructions.data, module->constants.data, module->stack_size, 0);
	else {
		return BT_FALSE;
	}

	bt_String* str = bt_to_string(context, thread.stack[0]);
	printf("Module returned: '%s'\n", str->str);
	printf("-----------------------------------------------------\n");

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

	if (BT_IS_STRING(lhs)) {
		bt_String* lhs_str = BT_AS_STRING(lhs);
		bt_String* rhs_str = BT_AS_STRING(rhs);
		uint32_t length = lhs_str->len + rhs_str->len;

		char* added = thread->context->alloc(length + 1);
		memcpy(added, lhs_str->str, lhs_str->len);
		memcpy(added + lhs_str->len, rhs_str->str, rhs_str->len);
		added[length] = 0;

		*result = BT_VALUE_STRING(bt_make_string_moved(thread->context, added, length));
		return;
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

static __forceinline void bt_not(bt_Thread* thread, bt_Value* result, bt_Value rhs)
{
	if (BT_IS_BOOL(rhs)) {
		*result = BT_VALUE_BOOL(BT_IS_FALSE(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot 'not' non-bool value!");
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

static __forceinline void bt_eq(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	*result = bt_value_is_equal(lhs, rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
}

static __forceinline void bt_neq(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	*result = bt_value_is_equal(lhs, rhs) ? BT_VALUE_FALSE : BT_VALUE_TRUE;
}

static __forceinline void bt_lt(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_AS_NUMBER(lhs) < BT_AS_NUMBER(rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		return;
	}

	bt_runtime_error(thread, "Cannot subtract non-number value!");
}

static __forceinline void bt_lte(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_AS_NUMBER(lhs) <= BT_AS_NUMBER(rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		return;
	}

	bt_runtime_error(thread, "Cannot subtract non-number value!");
}

static __forceinline void bt_and(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_BOOL(lhs) && BT_IS_BOOL(rhs)) {
		*result = BT_VALUE_BOOL(BT_IS_TRUE(lhs) && BT_IS_TRUE(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot 'and' non-bool value!");
}

static __forceinline void bt_or(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_BOOL(lhs) && BT_IS_BOOL(rhs)) {
		*result = BT_VALUE_BOOL(BT_IS_TRUE(lhs) || BT_IS_TRUE(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot 'or' non-bool value!");
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
			(*(bt_ModuleImport**)bt_buffer_at(&thread->callstack[thread->depth - 1].module->imports, op.ubc))->value;
		NEXT;

	case BT_OP_MOVE: stack[op.a] = stack[op.b]; NEXT;

	case BT_OP_EXPORT: {
		bt_Value name = stack[op.a];
		bt_Value value = stack[op.b];
		bt_Type* type = BT_AS_OBJECT(stack[op.c]);

		bt_module_export(context, thread->callstack[thread->depth - 1].module, type, name, value);
	} NEXT;

	case BT_OP_CLOSE: {
		bt_Buffer upvals = bt_buffer_with_capacity(context, sizeof(bt_Value), op.c);
		bt_Fn* fn = BT_AS_OBJECT(stack[op.b]);
		for (uint8_t i = 0; i < op.c; i++) {
			bt_buffer_push(context, &upvals, stack + op.b + 1 + i);
		}
		bt_Closure* cl = BT_ALLOCATE(context, CLOSURE, bt_Closure);
		cl->fn = fn;
		cl->upvals = upvals;
		stack[op.a] = BT_VALUE_OBJECT(cl);
	} NEXT;

	case BT_OP_LOADUP: stack[op.a] = *(bt_Value*)bt_buffer_at(
		&((bt_Closure*)thread->callstack[thread->depth - 1].callable)->upvals, op.b);
		NEXT;

	case BT_OP_STOREUP: *(bt_Value*)bt_buffer_at(
		&((bt_Closure*)thread->callstack[thread->depth - 1].callable)->upvals, op.a) = stack[op.b];
		NEXT;

	case BT_OP_NEG: bt_neg(thread, stack + op.a, stack[op.b]);              NEXT;
	case BT_OP_ADD: bt_add(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;
	case BT_OP_SUB: bt_sub(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;
	case BT_OP_MUL: bt_mul(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;
	case BT_OP_DIV: bt_div(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;

	case BT_OP_EQ:  bt_eq(thread, stack + op.a, stack[op.b], stack[op.c]);  NEXT;
	case BT_OP_NEQ: bt_neq(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;
	case BT_OP_LT:  bt_lt(thread, stack + op.a, stack[op.b], stack[op.c]);  NEXT;
	case BT_OP_LTE: bt_lte(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;

	case BT_OP_AND: bt_and(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;
	case BT_OP_OR:  bt_or(thread, stack + op.a, stack[op.b], stack[op.c]); NEXT;
	case BT_OP_NOT: bt_not(thread, stack + op.a, stack[op.b]); NEXT;

	case BT_OP_LOAD_IDX: stack[op.a] = bt_table_get(BT_AS_OBJECT(stack[op.b]), stack[op.c]); NEXT;
	
	case BT_OP_EXISTS:   stack[op.a] = stack[op.b] == BT_VALUE_NULL ? BT_VALUE_FALSE : BT_VALUE_TRUE; NEXT;
	case BT_OP_COALESCE: stack[op.a] = stack[op.b] == BT_VALUE_NULL ? stack[op.c]    : stack[op.b];   NEXT;
	
	case BT_OP_CALL: {
		uint16_t old_top = thread->top;

		bt_Callable* obj = BT_AS_OBJECT(stack[op.b]);
		thread->top += op.b + 1;
		thread->callstack[thread->depth].callable = obj;
		thread->callstack[thread->depth].return_loc = op.a - thread->top;
		thread->callstack[thread->depth].argc = op.c;
		thread->depth++;

		if (obj->obj.type == BT_OBJECT_TYPE_FN) {
			bt_Fn* callable = (bt_Fn*)obj;
			thread->callstack[thread->depth - 1].module = callable->module;
			bt_call(context, thread, callable->instructions.data, callable->constants.data, callable->stack_size, op.a - thread->top);
		}
		else if (obj->obj.type == BT_OBJECT_TYPE_CLOSURE) {
			bt_Closure* cl = obj;
			bt_Fn* callable = cl->fn;
			thread->callstack[thread->depth - 1].module = callable->module;
			bt_call(context, thread, callable->instructions.data, callable->constants.data, callable->stack_size, op.a - thread->top);
		}
		else if (obj->obj.type == BT_OBJECT_TYPE_NATIVE_FN) {
			bt_NativeFn* callable = (bt_NativeFn*)obj;
			callable->fn(context, thread);
			thread->depth--; // Manually pop the virtual call frame
		}
		else {
			bt_runtime_error(thread, "Unsupported callable type.");
		}

		thread->top = old_top;
	} NEXT;

	case BT_OP_JMP: ip += op.ibc; NEXT;
	case BT_OP_JMPF: if (stack[op.a] == BT_VALUE_FALSE) ip += op.ibc; NEXT;

	case BT_OP_RETURN: stack[return_loc] = stack[op.a]; RETURN;
	case BT_OP_END: RETURN;
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