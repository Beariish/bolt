#include "bolt.h"

#include "bt_object.h"
#include <assert.h>
#include <stdio.h>
#include <memory.h>
#include <immintrin.h>

#include "bt_tokenizer.h"
#include "bt_parser.h"
#include "bt_compiler.h"
#include "bt_debug.h"
#include "bt_gc.h"

#include "uperf/uperf.h"

static bt_Type* make_primitive_type(bt_Context* ctx, const char* name, bt_TypeSatisfier satisfier)
{
	return bt_make_type(ctx, name, satisfier, BT_TYPE_CATEGORY_PRIMITIVE, BT_FALSE);
}

void bt_open(bt_Context* context, bt_Alloc allocator, bt_Realloc realloc, bt_Free free)
{
	context->alloc = allocator;
	context->free = free;
	context->realloc = realloc;

	context->gc = bt_make_gc(context);

	context->n_allocated = 0;
	context->next = 0;
	context->root = bt_allocate(context, sizeof(bt_Object), BT_OBJECT_TYPE_NONE);
	context->next = context->root;
	context->troot_top = 0;

	context->current_thread = 0;
	
	context->types.number = make_primitive_type(context, "number", bt_type_satisfier_same);
	context->types.boolean = make_primitive_type(context, "bool", bt_type_satisfier_same);
	context->types.string = make_primitive_type(context, "string", bt_type_satisfier_same);
	context->types.table = bt_make_tableshape(context, "table", BT_FALSE);

	context->types.any = make_primitive_type(context, "any", bt_type_satisfier_any);
	context->types.null = make_primitive_type(context, "null", bt_type_satisfier_null);
	context->types.array = bt_make_array_type(context, context->types.any);

	context->types.type = bt_make_fundamental(context);
	context->types.type->as.type.boxed = context->types.any;

	context->loaded_modules = bt_make_table(context, 1);
	context->prelude = bt_make_table(context, 16);

	context->type_registry = bt_make_table(context, 16);
	bt_register_type(context, BT_VALUE_OBJECT(bt_make_string_hashed(context, "number")), context->types.number);
	bt_register_type(context, BT_VALUE_OBJECT(bt_make_string_hashed(context, "bool")), context->types.boolean);
	bt_register_type(context, BT_VALUE_OBJECT(bt_make_string_hashed(context, "string")), context->types.string);
	bt_register_type(context, BT_VALUE_OBJECT(bt_make_string_hashed(context, "table")), context->types.table);
	bt_register_type(context, BT_VALUE_OBJECT(bt_make_string_hashed(context, "any")), context->types.any);
	bt_register_type(context, BT_VALUE_OBJECT(bt_make_string_hashed(context, "null")), context->types.null);
	bt_register_type(context, BT_VALUE_OBJECT(bt_make_string_hashed(context, "array")), context->types.array);
	bt_register_type(context, BT_VALUE_OBJECT(bt_make_string_hashed(context, "Type")), context->types.type);

	context->meta_names.add = bt_make_string_hashed_len(context, "@add", 4);
	context->meta_names.sub = bt_make_string_hashed_len(context, "@sub", 4);
	context->meta_names.mul = bt_make_string_hashed_len(context, "@mul", 4);
	context->meta_names.div = bt_make_string_hashed_len(context, "@div", 4);
	context->meta_names.format = bt_make_string_hashed_len(context, "@format", 7);
	context->meta_names.collect = bt_make_string_hashed_len(context, "@collect", 8);

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
	UPERF_EVENT("Compile module");
#ifdef BOLT_PRINT_DEBUG
	printf("%s\n", source);
	printf("-----------------------------------------------------\n");
#endif

	UPERF_EVENT("Open Tokenizer");
	bt_Tokenizer* tok = context->alloc(sizeof(bt_Tokenizer));
	*tok = bt_open_tokenizer(context);
	bt_tokenizer_set_source(tok, source);
	UPERF_POP();

	UPERF_EVENT("Parse source");
	bt_Parser* parser = context->alloc(sizeof(bt_Parser));
	*parser = bt_open_parser(tok);
	if (bt_parse(parser) == BT_FALSE) {
		bt_close_parser(parser);
		bt_close_tokenizer(tok);
		context->free(parser);
		context->free(tok);
		return NULL;
	}
	UPERF_POP();

#ifdef BOLT_PRINT_DEBUG
	printf("-----------------------------------------------------\n");
#endif 

	UPERF_EVENT("Compile source");
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
	UPERF_POP();

#ifdef BOLT_PRINT_DEBUG
	UPERF_EVENT("Print compiler debug output");
	bt_debug_print_module(context, result);
	printf("-----------------------------------------------------\n");
	UPERF_POP();
#endif

	UPERF_EVENT("Cleanup");
	bt_close_compiler(compiler);
	bt_close_parser(parser);
	bt_close_tokenizer(tok);

	context->free(compiler);
	context->free(parser);
	context->free(tok);
	UPERF_POP();
	UPERF_POP();

	return result;
}

void bt_push_root(bt_Context* ctx, bt_Object* root)
{
	ctx->troots[ctx->troot_top++] = root;
}

void bt_pop_root(bt_Context* ctx)
{
	ctx->troot_top--;
}

bt_Object* bt_allocate(bt_Context* context, uint32_t full_size, bt_ObjectType type)
{
	bt_Object* obj = context->alloc(full_size);
	memset(obj, 0, full_size);
	obj->mark = 0;
	obj->type = type;
	obj->next = 0;

//	bt_grey_obj(context, obj);

	if (context->next) {
		BT_OBJECT_SET_NEXT(context->next, obj);
	}
	context->next = obj;

	context->gc.byets_allocated += full_size;
	if (context->gc.byets_allocated >= context->gc.next_cycle) {
		bt_push_root(context, obj);
		bt_collect(&context->gc, 0);
		bt_pop_root(context);
	}

	return obj;
}

static void free_subobjects(bt_Context* context, bt_Object* obj)
{
	switch (obj->type) {
	case BT_OBJECT_TYPE_TYPE: {
		bt_Type* type = obj;
		if (type->name) {
			switch (type->category) {
			case BT_TYPE_CATEGORY_SIGNATURE:
				if (!type->is_optional) {
					bt_buffer_destroy(context, &type->as.fn.args);
				}

				break;
			case BT_TYPE_CATEGORY_UNION:
				bt_buffer_destroy(context, &type->as.selector.types);
				break;
			case BT_TYPE_CATEGORY_USERDATA:
				bt_buffer_destroy(context, &type->as.userdata.fields);
				bt_buffer_destroy(context, &type->as.userdata.functions);
				break;
			}
			context->free(type->name);
		}
	} break;
	case BT_OBJECT_TYPE_STRING: {
		bt_String* string = obj;
		context->free(string->str);
	} break;
	case BT_OBJECT_TYPE_MODULE: {
		bt_Module* mod = obj;
		bt_buffer_destroy(context, &mod->constants);
		bt_buffer_destroy(context, &mod->instructions);
		bt_buffer_destroy(context, &mod->imports);
	} break;
	case BT_OBJECT_TYPE_FN: {
		bt_Fn* fn = obj;
		bt_buffer_destroy(context, &fn->constants);
		bt_buffer_destroy(context, &fn->instructions);
	} break;
	case BT_OBJECT_TYPE_CLOSURE: {
		bt_Closure* cl = obj;
		bt_buffer_destroy(context, &cl->upvals);
	} break;
	case BT_OBJECT_TYPE_TABLE: {
		bt_Table* table = obj;

		bt_Value collect_fn = bt_table_get(table, BT_VALUE_OBJECT(context->meta_names.collect));
		if (collect_fn != BT_VALUE_NULL) {
			bt_push(context->current_thread, collect_fn);
			bt_push(context->current_thread, BT_VALUE_OBJECT(table));
			bt_call(context->current_thread, 1);
		}

		bt_buffer_destroy(context, &table->pairs);
	} break;
	case BT_OBJECT_TYPE_ARRAY: {
		bt_Array* arr = obj;
		bt_buffer_destroy(context, &arr->items);
	} break;
	case BT_OBJECT_TYPE_USERDATA: {
		bt_Userdata* userdata = obj;
		context->free(userdata->data);
	} break;
	}
}

static uint32_t get_object_size(bt_Object* obj)
{
	switch (obj->type) {
	case BT_OBJECT_TYPE_NONE: return sizeof(bt_Object);
	case BT_OBJECT_TYPE_TYPE: return sizeof(bt_Type);
	case BT_OBJECT_TYPE_STRING: return sizeof(bt_String);
	case BT_OBJECT_TYPE_MODULE: return sizeof(bt_Module);
	case BT_OBJECT_TYPE_IMPORT: return sizeof(bt_ModuleImport);
	case BT_OBJECT_TYPE_FN: return sizeof(bt_Fn);
	case BT_OBJECT_TYPE_NATIVE_FN: return sizeof(bt_NativeFn);
	case BT_OBJECT_TYPE_CLOSURE: return sizeof(bt_Closure);
	case BT_OBJECT_TYPE_METHOD: return sizeof(bt_Fn);
	case BT_OBJECT_TYPE_ARRAY: return sizeof(bt_Array);
	case BT_OBJECT_TYPE_TABLE: return sizeof(bt_Table);
	case BT_OBJECT_TYPE_USERDATA: return sizeof(bt_Userdata);
	}

	assert(0);
	return 0;
}

void bt_free(bt_Context* context, bt_Object* obj)
{
	context->gc.byets_allocated -= get_object_size(obj);
	free_subobjects(context, obj);
	context->free(obj);

	//memset(obj, 0, sizeof(bt_Object));
}

void bt_register_type(bt_Context* context, bt_Value name, bt_Type* type)
{
	bt_table_set(context, context->type_registry, name, BT_VALUE_OBJECT(type));
	bt_register_prelude(context, name, bt_make_alias(context, 0, type), BT_VALUE_OBJECT(type));
}

bt_Type* bt_find_type(bt_Context* context, bt_Value name)
{
	return (bt_Type*)BT_AS_OBJECT(bt_table_get(context->type_registry, name));
}

void bt_register_prelude(bt_Context* context, bt_Value name, bt_Type* type, bt_Value value)
{
	bt_ModuleImport* new_import = BT_ALLOCATE(context, IMPORT, bt_ModuleImport);
	new_import->name = BT_AS_OBJECT(name);
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
	UPERF_EVENT("Find module");
	// TODO: resolve module name with path
	bt_Module* mod = BT_AS_OBJECT(bt_table_get(context->loaded_modules, name));
	if (mod == 0) {
		UPERF_EVENT("Locate source");
		bt_String* to_load = BT_AS_OBJECT(name);
		
		char* name_as_bolt_file = context->alloc(to_load->len + 5 + 1);
		memcpy(name_as_bolt_file, to_load->str, to_load->len);
		memcpy(name_as_bolt_file + to_load->len, ".bolt", 5);
		name_as_bolt_file[to_load->len + 5] = 0;

		FILE* source;
		fopen_s(&source, name_as_bolt_file, "rb");
		//context->free(name_as_bolt_file);
		UPERF_POP();

		if (source == 0) {
			assert(0 && "Cannot find module file!");
			return NULL;
		}

		UPERF_EVENT("Read source");
		fseek(source, 0, SEEK_END);
		uint32_t len = ftell(source);
		fseek(source, 0, SEEK_SET);

		char* code = context->alloc(len + 1);
		fread(code, 1, len, source);
		fclose(source);
		code[len] = 0;
		UPERF_POP();

		bt_Module* new_mod = bt_compile_module(context, code);
		context->free(code);

		if (new_mod) {
			UPERF_EVENT("Execute module");
			bt_register_module(context, name, new_mod);
			bt_execute(context, new_mod);
			UPERF_POP();

			//bt_collect(&context->gc, 0);

			UPERF_POP();
			return new_mod;
		}
		else {
			UPERF_POP();
			return NULL;
		}
	}

	UPERF_POP();
	return mod;
}

static void call(bt_Context* context, bt_Thread* thread, bt_Callable* callable, bt_Module* module, bt_Op* ip, bt_Value* constants, int8_t return_loc);

bt_bool bt_execute(bt_Context* context, bt_Module* module)
{
	bt_Thread thread;
	thread.depth = 0;
	thread.top = 0;
	thread.context = context;

	thread.callstack[thread.depth].return_loc = 0;
	thread.callstack[thread.depth].argc = 0;
	thread.callstack[thread.depth].size = module->stack_size;
	thread.callstack[thread.depth].callable = module;
	thread.callstack[thread.depth].user_top = 0;
	thread.depth++;

	context->current_thread = &thread;

	int32_t result = setjmp(thread.error_loc);

	if(result == 0) call(context, &thread, module, module, module->instructions.data, module->constants.data, 0);
	else {
		return BT_FALSE;
	}

#ifdef BOLT_PRINT_DEBUG
	bt_String* str = bt_to_string(context, thread.stack[0]);
	printf("Module returned: '%s'\n", str->str);
	printf("-----------------------------------------------------\n");
#endif

	context->current_thread = 0;

	return BT_TRUE;
}

void bt_runtime_error(bt_Thread* thread, const char* message)
{
	printf("BOLT ERROR: %s\n", message);
	longjmp(thread->error_loc, 1);
}

void bt_push(bt_Thread* thread, bt_Value value)
{
	bt_StackFrame* frame = &thread->callstack[thread->depth - 1];
	thread->stack[thread->top + frame->size + (++frame->user_top)] = value;
}

bt_Value bt_pop(bt_Thread* thread)
{
	bt_StackFrame* frame = &thread->callstack[thread->depth - 1];
	return thread->stack[thread->top + frame->size + frame->user_top--];
}

bt_Value bt_make_closure(bt_Thread* thread, uint8_t num_upvals)
{
	bt_StackFrame* frame = thread->callstack + thread->depth - 1;
	bt_Value* true_top = thread->stack + thread->top + frame->size + frame->user_top;

	bt_Buffer upvals = bt_buffer_with_capacity(thread->context, sizeof(bt_Value), num_upvals);
	for (bt_Value* top = true_top - (num_upvals - 1); top <= true_top; top++) {
		bt_buffer_push(thread->context, &upvals, top);
	}

	bt_Closure* cl = BT_ALLOCATE(thread->context, CLOSURE, bt_Closure);
	cl->fn = BT_AS_OBJECT(*(true_top - num_upvals));
	cl->upvals = upvals;

	frame->user_top -= num_upvals + 1;

	return BT_VALUE_OBJECT(cl);
}

void bt_call(bt_Thread* thread, uint8_t argc)
{
	uint16_t old_top = thread->top;

	bt_StackFrame* frame = &thread->callstack[thread->depth - 1];
	frame->user_top -= argc;

	thread->top += frame->size + 2;
	bt_Object* obj = BT_AS_OBJECT(thread->stack[thread->top - 1]);

	thread->callstack[thread->depth].return_loc = -1;
	thread->callstack[thread->depth].argc = argc;
	thread->callstack[thread->depth].callable = obj;
	thread->callstack[thread->depth].user_top = 0;
	thread->depth++;

	if (obj->type == BT_OBJECT_TYPE_FN) {
		bt_Fn* callable = (bt_Fn*)obj;
		thread->callstack[thread->depth - 1].size = callable->stack_size;
		call(thread->context, thread, callable, callable->module, callable->instructions.data, callable->constants.data, -1);
	}
	else if (obj->type == BT_OBJECT_TYPE_CLOSURE) {
		bt_Fn* callable = ((bt_Closure*)obj)->fn;
		thread->callstack[thread->depth - 1].size = callable->stack_size;
		call(thread->context, thread, (bt_Closure*)obj, callable->module, callable->instructions.data, callable->constants.data, -1);
	}
	else if (obj->type == BT_OBJECT_TYPE_NATIVE_FN) {
		bt_NativeFn* callable = (bt_NativeFn*)obj;
		callable->fn(thread->context, thread);
	}
	else {
		bt_runtime_error(thread, "Unsupported callable type.");
	}

	thread->depth--;
	thread->top = old_top;
}

#define XSTR(x) #x
#define ARITH_MF(name)                                                                               \
if (BT_IS_OBJECT(lhs)) {																			 \
	bt_Object* obj = BT_AS_OBJECT(lhs);																 \
	if (obj->type == BT_OBJECT_TYPE_TABLE) {														 \
		bt_Table* tbl = obj;																		 \
		bt_Value add_fn = bt_table_get(tbl, BT_VALUE_OBJECT(thread->context->meta_names.name));		 \
		if (add_fn == BT_VALUE_NULL) bt_runtime_error(thread, "Unable to find @" XSTR(name) "metafunction!");	 \
																									 \
		bt_push(thread, add_fn);																	 \
		bt_push(thread, lhs);																		 \
		bt_push(thread, rhs);																		 \
		bt_call(thread, 2);																			 \
		*result = bt_pop(thread);																	 \
																									 \
		return;																						 \
	}																								 \
}

static __declspec(noinline) void bt_add(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) + BT_AS_NUMBER(rhs));
		return;
	}

	if (BT_IS_OBJECT(lhs) && BT_IS_OBJECT(rhs)) {
		bt_String* lhs_str = BT_AS_OBJECT(lhs);
		bt_String* rhs_str = BT_AS_OBJECT(rhs);

		if (lhs_str->obj.type == BT_OBJECT_TYPE_STRING && rhs_str->obj.type == BT_OBJECT_TYPE_STRING) {
			uint32_t length = lhs_str->len + rhs_str->len;

			char* added = thread->context->alloc(length + 1);
			memcpy(added, lhs_str->str, lhs_str->len);
			memcpy(added + lhs_str->len, rhs_str->str, rhs_str->len);
			added[length] = 0;

			*result = BT_VALUE_OBJECT(bt_make_string_moved(thread->context, added, length));
			return;
		}
	}

	ARITH_MF(add);

	if (BT_TYPEOF(lhs) != BT_TYPEOF(rhs)) {
		bt_runtime_error(thread, "Cannot add separate types!");
	}

	bt_runtime_error(thread, "Unable to add values of type <TODO>!");
}

static BT_FORCE_INLINE void bt_neg(bt_Thread* thread, bt_Value* result, bt_Value rhs)
{
	if (BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(-BT_AS_NUMBER(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot negate non-number value!");
}

static BT_FORCE_INLINE void bt_not(bt_Thread* thread, bt_Value* result, bt_Value rhs)
{
	if (BT_IS_BOOL(rhs)) {
		*result = BT_VALUE_BOOL(BT_IS_FALSE(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot 'not' non-bool value!");
}

static BT_FORCE_INLINE void bt_sub(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) - BT_AS_NUMBER(rhs));
		return;
	}

	ARITH_MF(sub);

	bt_runtime_error(thread, "Cannot subtract non-number value!");
}
static __declspec(noinline) void bt_mul(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) * BT_AS_NUMBER(rhs));
		return;
	}

	ARITH_MF(mul);

	bt_runtime_error(thread, "Cannot multiply non-number value!");
}

static __declspec(noinline) void bt_div(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) / BT_AS_NUMBER(rhs));
		return;
	}

	ARITH_MF(div);

	bt_runtime_error(thread, "Cannot divide non-number value!");
}

static BT_FORCE_INLINE void bt_eq(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	*result = bt_value_is_equal(lhs, rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
}

static BT_FORCE_INLINE void bt_neq(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	*result = bt_value_is_equal(lhs, rhs) ? BT_VALUE_FALSE : BT_VALUE_TRUE;
}

static __declspec(noinline) void bt_lt(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_AS_NUMBER(lhs) < BT_AS_NUMBER(rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		return;
	}

	bt_runtime_error(thread, "Cannot lt non-number value!");
}

static BT_FORCE_INLINE void bt_lte(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_AS_NUMBER(lhs) <= BT_AS_NUMBER(rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		return;
	}

	bt_runtime_error(thread, "Cannot lte non-number value!");
}

static BT_FORCE_INLINE void bt_and(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_BOOL(lhs) && BT_IS_BOOL(rhs)) {
		*result = BT_VALUE_BOOL(BT_IS_TRUE(lhs) && BT_IS_TRUE(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot 'and' non-bool value!");
}

static BT_FORCE_INLINE void bt_or(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs)
{
	if (BT_IS_BOOL(lhs) && BT_IS_BOOL(rhs)) {
		*result = BT_VALUE_BOOL(BT_IS_TRUE(lhs) || BT_IS_TRUE(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot 'or' non-bool value!");
}

static void call(bt_Context* context, bt_Thread* thread, bt_Callable* callable, bt_Module* module, bt_Op* ip, bt_Value* constants, int8_t return_loc)
{
	bt_Value* stack = thread->stack + thread->top;
	_mm_prefetch(stack, 1);
	bt_Value* upv = thread->callstack[thread->depth - 1].upvals;
	bt_Object* obj;
	bt_Op op;

#define NEXT break;
#define RETURN return;
	for (;;) {
		op = *ip++;
		switch (op.op) {
		case BT_OP_LOAD:        stack[op.a] = constants[op.b];                       NEXT;
		case BT_OP_LOAD_SMALL:  stack[op.a] = BT_VALUE_NUMBER(op.ibc);               NEXT;
		case BT_OP_LOAD_NULL:   stack[op.a] = BT_VALUE_NULL;                         NEXT;
		case BT_OP_LOAD_BOOL:   stack[op.a] = op.b ? BT_VALUE_TRUE : BT_VALUE_FALSE; NEXT;
		case BT_OP_LOAD_IMPORT:
			stack[op.a] =
				(*(bt_ModuleImport**)bt_buffer_at(&module->imports, op.ubc))->value;
			NEXT;

		case BT_OP_TABLE: stack[op.a] = BT_VALUE_OBJECT(bt_make_table(context, op.ibc)); NEXT;
		case BT_OP_TTABLE: {
			bt_Table* tbl = bt_make_table(context, op.b);
			tbl->prototype = bt_type_get_proto(context, BT_AS_OBJECT(stack[op.c]));
			stack[op.a] = BT_VALUE_OBJECT(tbl);
		} NEXT;

		case BT_OP_ARRAY: {
			bt_Array* arr = bt_make_array(context, op.ibc);
			arr->items.length = op.ibc;
			stack[op.a] = BT_VALUE_OBJECT(arr);
		} NEXT;

		case BT_OP_MOVE: stack[op.a] = stack[op.b]; NEXT;

		case BT_OP_EXPORT: {
			bt_module_export(context, module, BT_AS_OBJECT(stack[op.c]), stack[op.a], stack[op.b]);
		} NEXT;

		case BT_OP_CLOSE: {
			bt_Buffer upvals = bt_buffer_with_capacity(context, sizeof(bt_Value), op.c);
			obj = BT_AS_OBJECT(stack[op.b]);
			for (uint8_t i = 0; i < op.c; i++) {
				bt_buffer_push(context, &upvals, stack + op.b + 1 + i);
			}
			bt_Closure* cl = BT_ALLOCATE(context, CLOSURE, bt_Closure);
			cl->fn = obj;
			cl->upvals = upvals;
			stack[op.a] = BT_VALUE_OBJECT(cl);
		} NEXT;

		case BT_OP_LOADUP: BT_ASSUME(upv); stack[op.a] = upv[op.b];  NEXT;
		case BT_OP_STOREUP: BT_ASSUME(upv); upv[op.a] = stack[op.b]; NEXT;

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

		case BT_OP_LOAD_IDX: stack[op.a] = bt_get(context, BT_AS_OBJECT(stack[op.b]), stack[op.c]); NEXT;
		case BT_OP_STORE_IDX: bt_set(context, BT_AS_OBJECT(stack[op.a]), stack[op.b], stack[op.c]); NEXT;

		case BT_OP_LOAD_IDX_K: stack[op.a] = bt_get(context, BT_AS_OBJECT(stack[op.b]), constants[op.c]); NEXT;
		case BT_OP_STORE_IDX_K: bt_set(context, BT_AS_OBJECT(stack[op.a]), constants[op.b], stack[op.c]); NEXT;

		case BT_OP_EXPECT:   stack[op.a] = stack[op.b]; if (stack[op.a] == BT_VALUE_NULL) bt_runtime_error(thread, "Operator '!' failed - lhs was null!"); NEXT;
		case BT_OP_EXISTS:   stack[op.a] = stack[op.b] == BT_VALUE_NULL ? BT_VALUE_FALSE : BT_VALUE_TRUE; NEXT;
		case BT_OP_COALESCE: stack[op.a] = stack[op.b] == BT_VALUE_NULL ? stack[op.c] : stack[op.b];   NEXT;

		case BT_OP_TCHECK: {
			stack[op.a] = bt_is_type(stack[op.b], BT_AS_OBJECT(stack[op.c])) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		} NEXT;

		case BT_OP_TCAST: {
			if (bt_is_type(stack[op.b], BT_AS_OBJECT(stack[op.c]))) {
				stack[op.a] = bt_cast_type(stack[op.b], BT_AS_OBJECT(stack[op.c]));
			}
			else {
				stack[op.a] = BT_VALUE_NULL;
			}
		} NEXT;

		case BT_OP_TALIAS: {
			bt_Table* tbl = BT_AS_OBJECT(stack[op.b]);
			tbl->prototype = bt_type_get_proto(context, BT_AS_OBJECT(stack[op.c]));
			stack[op.a] = stack[op.b];
		} NEXT;

		case BT_OP_COMPOSE: {
			bt_Table* lhs = BT_AS_OBJECT(stack[op.b]);
			bt_Table* rhs = BT_AS_OBJECT(stack[op.c]);
			bt_Table* result = bt_make_table(context, lhs->pairs.length + rhs->pairs.length);
			bt_buffer_append(context, &result->pairs, &lhs->pairs);
			bt_buffer_append(context, &result->pairs, &rhs->pairs);
			stack[op.a] = BT_VALUE_OBJECT(result);
		} NEXT;

		case BT_OP_CALL: {
			uint16_t old_top = thread->top;

			obj = BT_AS_OBJECT(stack[op.b]);

			thread->top += op.b + 1;
			thread->callstack[thread->depth].return_loc = op.a - (op.b + 1);
			thread->callstack[thread->depth].argc = op.c;
			thread->callstack[thread->depth].callable = obj;
			thread->callstack[thread->depth].user_top = 0;
			thread->depth++;

			if (obj->type == BT_OBJECT_TYPE_FN) {
				bt_Fn* callable = (bt_Fn*)obj;
				thread->callstack[thread->depth - 1].size = callable->stack_size;
				call(context, thread, callable, callable->module, callable->instructions.data, callable->constants.data, op.a - (op.b + 1));
			}
			else if (obj->type == BT_OBJECT_TYPE_CLOSURE) {
				bt_Fn* callable = ((bt_Closure*)obj)->fn;
				thread->callstack[thread->depth - 1].size = callable->stack_size;
				thread->callstack[thread->depth - 1].upvals = ((bt_Closure*)obj)->upvals.data;

				if (callable->obj.type == BT_OBJECT_TYPE_FN) {
					call(context, thread, (bt_Closure*)obj, callable->module, callable->instructions.data, callable->constants.data, op.a - (op.b + 1));
				}
				else if (callable->obj.type == BT_OBJECT_TYPE_NATIVE_FN) {
					thread->callstack[thread->depth - 1].size = 0;
					bt_NativeFn* as_native = callable;
					as_native->fn(context, thread);
				}
				else {
					bt_runtime_error(thread, "Closure contained unsupported callable type.");
				}
			}
			else if (obj->type == BT_OBJECT_TYPE_NATIVE_FN) {
				thread->callstack[thread->depth - 1].size = 0;
				bt_NativeFn* callable = (bt_NativeFn*)obj;
				callable->fn(context, thread);
			}
			else {
				bt_runtime_error(thread, "Unsupported callable type.");
			}

			thread->depth--;
			thread->top = old_top;
		} NEXT;

		case BT_OP_JMP: ip += op.ibc; NEXT;
		case BT_OP_JMPF: if (stack[op.a] == BT_VALUE_FALSE) ip += op.ibc; NEXT;

		case BT_OP_RETURN: stack[return_loc] = stack[op.a]; RETURN;
		case BT_OP_END: RETURN;

		case BT_OP_NUMFOR: {
			stack[op.a] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[op.a]) + BT_AS_NUMBER(stack[op.a + 1]));
			if (stack[op.a] >= stack[op.a + 2]) ip += op.ibc; 
		} NEXT;

		case BT_OP_ITERFOR: {
			bt_Closure* cl = BT_AS_OBJECT(stack[op.a + 1]);
			BT_ASSUME(cl);
			thread->top += op.a + 2;
			thread->depth++;
			thread->callstack[thread->depth - 1].upvals = cl->upvals.data;

			if (cl->fn->obj.type == BT_OBJECT_TYPE_FN) {
				call(context, thread, cl, cl->fn->module, cl->fn->instructions.data, cl->fn->constants.data, op.a - thread->top);
			}
			else {
				((bt_NativeFn*)cl->fn)->fn(context, thread);
			}
			thread->top -= op.a + 2;
			if (stack[op.a] == BT_VALUE_NULL) { ip += op.ibc; }
		} NEXT;
		case BT_OP_ADDF: {
			stack[op.a] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[op.b]) + BT_AS_NUMBER(stack[op.c]));
		} NEXT;
		case BT_OP_SUBF: {
			stack[op.a] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[op.b]) - BT_AS_NUMBER(stack[op.c]));
		} NEXT;
		case BT_OP_MULF: {
			stack[op.a] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[op.b]) * BT_AS_NUMBER(stack[op.c]));
		} NEXT;
		case BT_OP_DIVF: {
			stack[op.a] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[op.b]) / BT_AS_NUMBER(stack[op.c]));
		} NEXT;
		case BT_OP_LTF:  stack[op.a] = BT_VALUE_FALSE + (BT_AS_NUMBER(stack[op.b]) < BT_AS_NUMBER(stack[op.c])); NEXT;
		case BT_OP_LOAD_SUB_F: {
			stack[op.a] = bt_array_get(context, BT_AS_OBJECT(stack[op.b]), BT_AS_NUMBER(stack[op.c]));
		} NEXT;
		default: BT_ASSUME(0);
		}
	}
}