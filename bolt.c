#include "bolt.h"

#include "bt_object.h"
#include <assert.h>
#include <stdio.h>
#include <memory.h>
#include <immintrin.h>
#include <string.h>

#include "bt_tokenizer.h"
#include "bt_parser.h"
#include "bt_compiler.h"
#include "bt_debug.h"
#include "bt_gc.h"

static bt_Type* make_primitive_type(bt_Context* ctx, const char* name, bt_TypeSatisfier satisfier)
{
	return bt_make_type(ctx, name, satisfier, BT_TYPE_CATEGORY_PRIMITIVE, BT_FALSE);
}

void bt_open(bt_Context* context, bt_Alloc allocator, bt_Realloc realloc, bt_Free free, bt_ErrorFunc error)
{
	context->alloc = allocator;
	context->free = free;
	context->realloc = realloc;
	context->on_error = error;

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

	context->compiler_options.generate_debug_info = BT_TRUE;

	context->module_paths = NULL;
	bt_append_module_path(context, "%s.bolt");
	bt_append_module_path(context, "%s/module.bolt");

	context->is_valid = BT_TRUE;
}

void bt_close(bt_Context* context)
{
	bt_Object* obj = context->root;

	while (obj) {
		bt_Object* next = BT_OBJECT_NEXT(obj);
		bt_free(context, obj);
		obj = next;
	}

	bt_Path* path = context->module_paths;
	while (path) {
		bt_Path* next = path->next;
		context->free(path->spec);
		context->free(path);
		path = next;
	}

	bt_destroy_gc(context, &context->gc);
}

bt_bool bt_run(bt_Context* context, const char* source)
{
	bt_Module* mod = bt_compile_module(context, source);
	if (!mod) return BT_FALSE;
	return bt_execute(context, mod);
}

bt_Module* bt_compile_module(bt_Context* context, const char* source)
{
#ifdef BOLT_PRINT_DEBUG
	printf("%s\n", source);
	printf("-----------------------------------------------------\n");
#endif

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

#ifdef BOLT_PRINT_DEBUG
	printf("-----------------------------------------------------\n");
#endif 

	bt_Compiler* compiler = context->alloc(sizeof(bt_Compiler));
	*compiler = bt_open_compiler(parser, context->compiler_options);
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

#ifdef BOLT_PRINT_DEBUG
	bt_debug_print_module(context, result);
	printf("-----------------------------------------------------\n");
#endif

	bt_close_compiler(compiler);
	bt_close_parser(parser);
	bt_close_tokenizer(tok);

	context->free(compiler);
	context->free(parser);
	context->free(tok);

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

	BT_OBJECT_SET_TYPE(obj, type);

	bt_ObjectType g_type = BT_OBJECT_GET_TYPE(obj);

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
	switch (BT_OBJECT_GET_TYPE(obj)) {
	case BT_OBJECT_TYPE_TYPE: {
		bt_Type* type = obj;
		if (type->name) {
			switch (type->category) {
			case BT_TYPE_CATEGORY_SIGNATURE:
				bt_buffer_destroy(context, &type->as.fn.args);
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

		if (mod->debug_locs) {
			bt_buffer_destroy(context, mod->debug_locs);

			for (uint32_t i = 0; i < mod->debug_tokens.length; i++) {
				context->free(mod->debug_tokens.elements[i]);
			}

			bt_buffer_destroy(context, &mod->debug_tokens);
			context->free(mod->debug_locs);
			context->free(mod->debug_source);
		}
	} break;
	case BT_OBJECT_TYPE_FN: {
		bt_Fn* fn = obj;
		bt_buffer_destroy(context, &fn->constants);
		bt_buffer_destroy(context, &fn->instructions);
		if (fn->debug) {
			bt_buffer_destroy(context, fn->debug);
			context->free(fn->debug);
		}
	} break;
	case BT_OBJECT_TYPE_CLOSURE: {
		bt_Closure* cl = obj;
		bt_buffer_destroy(context, &cl->upvals);
	} break;
	case BT_OBJECT_TYPE_TABLE: {
		bt_Table* table = obj;
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
	switch (BT_OBJECT_GET_TYPE(obj)) {
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

void bt_append_module_path(bt_Context* context, const char* spec)
{
	bt_Path** ptr_next = &context->module_paths;
	while (*ptr_next) {
		ptr_next = &(*ptr_next)->next;
	}

	bt_Path* actual_next = context->alloc(sizeof(bt_Path));
	actual_next->spec = context->alloc(strlen(spec) + 1);
	strcpy(actual_next->spec, spec);
	actual_next->next = NULL;

	*ptr_next = actual_next;
}

bt_Module* bt_find_module(bt_Context* context, bt_Value name)
{
	// TODO: resolve module name with path
	bt_Module* mod = BT_AS_OBJECT(bt_table_get(context->loaded_modules, name));
	if (mod == 0) {
		bt_String* to_load = BT_AS_OBJECT(name);
		
		char path_buf[256];
		uint32_t path_len = 0;
		FILE* source = NULL;
		
		bt_Path* pathspec = context->module_paths;
		while (pathspec && !source) {
			path_len = sprintf_s(path_buf, 256, pathspec->spec, to_load->str);

			if (path_len >= 256) {
				bt_runtime_error(context->current_thread, "Path buffer overrun when loading module!", NULL);
				return NULL;
			}

			path_buf[path_len] = 0;
			fopen_s(&source, path_buf, "rb");

			pathspec = pathspec->next;
		}

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
		new_mod->name = BT_AS_OBJECT(name);
		new_mod->path = bt_make_string_len(context, path_buf, path_len);
		context->free(code);

		if (new_mod) {
			if (bt_execute(context, new_mod)) {
				bt_register_module(context, name, new_mod);

				return new_mod;
			}
			else {
				return NULL;
			}
		}
		else {
			return NULL;
		}
	}

	return mod;
}

static void call(bt_Context* context, bt_Thread* thread, bt_Module* module, bt_Op* ip, bt_Value* constants, int8_t return_loc);

bt_bool bt_execute(bt_Context* context, bt_Module* module)
{
	bt_Thread* thread = context->alloc(sizeof(bt_Thread));
	thread->depth = 0;
	thread->top = 0;
	thread->context = context;

	thread->callstack[thread->depth].return_loc = 0;
	thread->callstack[thread->depth].argc = 0;
	thread->callstack[thread->depth].size = module->stack_size;
	thread->callstack[thread->depth].callable = module;
	thread->callstack[thread->depth].user_top = 0;
	thread->depth++;

	context->current_thread = thread;

	int32_t result = setjmp(&thread->error_loc);

	if(result == 0) call(context, thread, module, module->instructions.elements, module->constants.elements, 0);
	else {
		context->free(thread);
		return BT_FALSE;
	}

	context->current_thread = 0;
	context->free(thread);

	return BT_TRUE;
}

static bt_Module* get_module(bt_Callable* cb)
{
	switch (BT_OBJECT_GET_TYPE(&cb->obj)) {
	case BT_OBJECT_TYPE_FN: return cb->fn.module;
	case BT_OBJECT_TYPE_CLOSURE: return cb->cl.fn->module;
	case BT_OBJECT_TYPE_MODULE: return &cb->module;
	case BT_OBJECT_TYPE_NATIVE_FN: return NULL;
	default: assert(0); return NULL;
	}
}

void bt_runtime_error(bt_Thread* thread, const char* message, bt_Op* ip)
{
	if (ip != NULL) {
		bt_Callable* callable = thread->callstack[thread->depth - 1].callable;
		const char* source = bt_get_debug_source(callable);

		bt_DebugLocBuffer* loc_buffer = bt_get_debug_locs(callable);
		uint32_t loc_index = bt_get_debug_index(callable, ip);

		bt_TokenBuffer* tokens = bt_get_debug_tokens(callable);

		bt_Token* source_token = tokens->elements[loc_buffer->elements[loc_index]];
		bt_StrSlice line_source = bt_get_debug_line(source, source_token->line - 1);
		bt_Module* module = get_module(callable);

		thread->context->on_error(BT_ERROR_RUNTIME, (module && module->path) ? module->path->str : "", message, source_token->line - 1, source_token->col);
	}
	else {
		thread->context->on_error(BT_ERROR_RUNTIME, "<native>", message, 0, 0);
	}

	thread->context->current_thread = NULL;
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

	bt_ValueBuffer upvals;
	bt_buffer_with_capacity(&upvals, thread->context, num_upvals);
	for (bt_Value* top = true_top - (num_upvals - 1); top <= true_top; top++) {
		bt_buffer_push(thread->context, &upvals, *top);
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

	if (BT_OBJECT_GET_TYPE(obj) == BT_OBJECT_TYPE_FN) {
		bt_Fn* callable = (bt_Fn*)obj;
		thread->callstack[thread->depth - 1].size = callable->stack_size;
		call(thread->context, thread, callable->module, callable->instructions.elements, callable->constants.elements, -1);
	}
	else if (BT_OBJECT_GET_TYPE(obj) == BT_OBJECT_TYPE_CLOSURE) {
		bt_Fn* callable = ((bt_Closure*)obj)->fn;
		thread->callstack[thread->depth - 1].size = callable->stack_size;
		call(thread->context, thread, callable->module, callable->instructions.elements, callable->constants.elements, -1);
	}
	else if (BT_OBJECT_GET_TYPE(obj) == BT_OBJECT_TYPE_NATIVE_FN) {
		bt_NativeFn* callable = (bt_NativeFn*)obj;
		callable->fn(thread->context, thread);
	}
	else {
		bt_runtime_error(thread, "Unsupported callable type.", NULL);
	}

	thread->depth--;
	thread->top = old_top;
}

const char* bt_get_debug_source(bt_Callable* callable)
{
	switch (BT_OBJECT_GET_TYPE(callable)) {
	case BT_OBJECT_TYPE_FN:
		return callable->fn.module->debug_source;
	case BT_OBJECT_TYPE_MODULE:
		return callable->module.debug_source;
	case BT_OBJECT_TYPE_CLOSURE:
		return callable->cl.fn->module->debug_source;
	}

	return NULL;
}

bt_TokenBuffer* bt_get_debug_tokens(bt_Callable* callable)
{
	switch (BT_OBJECT_GET_TYPE(callable)) {
	case BT_OBJECT_TYPE_FN:
		return &callable->fn.module->debug_tokens;
	case BT_OBJECT_TYPE_MODULE:
		return &callable->module.debug_tokens;
	case BT_OBJECT_TYPE_CLOSURE:
		return &callable->cl.fn->module->debug_tokens;
	}

	return NULL;
}

bt_StrSlice bt_get_debug_line(const char* source, uint16_t line)
{
	uint16_t cur_line = 1;
	while (*source) {
		if (cur_line == line) {
			const char* line_start = source;
			while (*source && *source != '\n') source++;
			return (bt_StrSlice) { line_start, source - line_start };
		}

		if (*source == '\n') cur_line++;
		source++;
	}

	return (bt_StrSlice) { source, 0 };
}

bt_DebugLocBuffer* bt_get_debug_locs(bt_Callable* callable)
{
	switch (BT_OBJECT_GET_TYPE(callable)) {
	case BT_OBJECT_TYPE_FN:
		return callable->fn.debug;
	case BT_OBJECT_TYPE_MODULE:
		return callable->module.debug_locs;
	case BT_OBJECT_TYPE_CLOSURE:
		return callable->cl.fn->debug;
	}

	return NULL;
}

uint32_t bt_get_debug_index(bt_Callable* callable, bt_Op* ip)
{
	bt_InstructionBuffer* instructions = NULL;
	switch (BT_OBJECT_GET_TYPE(callable)) {
	case BT_OBJECT_TYPE_FN:
		instructions = &callable->fn.instructions;
		break;
	case BT_OBJECT_TYPE_MODULE:
		instructions = &callable->module.instructions;
		break;
	case BT_OBJECT_TYPE_CLOSURE:
		instructions = &callable->cl.fn->instructions;
		break;
	}

	if (instructions) {
		return ip - instructions->elements;
	}

	return 0;
}

#define XSTR(x) #x
#define ARITH_MF(name)                                                                               \
if (BT_IS_OBJECT(lhs)) {																			 \
	bt_Object* obj = BT_AS_OBJECT(lhs);																 \
	if (BT_OBJECT_GET_TYPE(obj) == BT_OBJECT_TYPE_TABLE) {														 \
		bt_Table* tbl = obj;																		 \
		bt_Value add_fn = bt_table_get(tbl, BT_VALUE_OBJECT(thread->context->meta_names.name));		 \
		if (add_fn == BT_VALUE_NULL) bt_runtime_error(thread, "Unable to find @" #name "metafunction!", ip);	 \
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

static __declspec(noinline) void bt_add(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) + BT_AS_NUMBER(rhs));
		return;
	}

	if (BT_IS_OBJECT(lhs) && BT_IS_OBJECT(rhs)) {
		bt_String* lhs_str = BT_AS_OBJECT(lhs);
		bt_String* rhs_str = BT_AS_OBJECT(rhs);

		if (BT_OBJECT_GET_TYPE(lhs_str) == BT_OBJECT_TYPE_STRING && BT_OBJECT_GET_TYPE(rhs_str) == BT_OBJECT_TYPE_STRING) {
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
		bt_runtime_error(thread, "Cannot add separate types!", ip);
	}

	bt_runtime_error(thread, "Unable to add values of type <TODO>!", ip);
}

static BT_FORCE_INLINE void bt_neg(bt_Thread* thread, bt_Value* result, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(-BT_AS_NUMBER(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot negate non-number value!", ip);
}

static BT_FORCE_INLINE void bt_not(bt_Thread* thread, bt_Value* result, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_BOOL(rhs)) {
		*result = BT_VALUE_BOOL(BT_IS_FALSE(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot 'not' non-bool value!", ip);
}

static BT_FORCE_INLINE void bt_sub(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) - BT_AS_NUMBER(rhs));
		return;
	}

	ARITH_MF(sub);

	bt_runtime_error(thread, "Cannot subtract non-number value!", ip);
}
static __declspec(noinline) void bt_mul(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) * BT_AS_NUMBER(rhs));
		return;
	}

	ARITH_MF(mul);

	bt_runtime_error(thread, "Cannot multiply non-number value!", ip);
}

static __declspec(noinline) void bt_div(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) / BT_AS_NUMBER(rhs));
		return;
	}

	ARITH_MF(div);

	bt_runtime_error(thread, "Cannot divide non-number value!", ip);
}

static BT_FORCE_INLINE void bt_eq(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	*result = bt_value_is_equal(lhs, rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
}

static BT_FORCE_INLINE void bt_neq(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	*result = bt_value_is_equal(lhs, rhs) ? BT_VALUE_FALSE : BT_VALUE_TRUE;
}

static __declspec(noinline) void bt_lt(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_AS_NUMBER(lhs) < BT_AS_NUMBER(rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		return;
	}

	bt_runtime_error(thread, "Cannot lt non-number value!", ip);
}

static BT_FORCE_INLINE void bt_lte(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_AS_NUMBER(lhs) <= BT_AS_NUMBER(rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		return;
	}

	bt_runtime_error(thread, "Cannot lte non-number value!", ip);
}

static BT_FORCE_INLINE void bt_and(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_BOOL(lhs) && BT_IS_BOOL(rhs)) {
		*result = BT_VALUE_BOOL(BT_IS_TRUE(lhs) && BT_IS_TRUE(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot 'and' non-bool value!", ip);
}

static BT_FORCE_INLINE void bt_or(bt_Thread* thread, bt_Value* result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_BOOL(lhs) && BT_IS_BOOL(rhs)) {
		*result = BT_VALUE_BOOL(BT_IS_TRUE(lhs) || BT_IS_TRUE(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot 'or' non-bool value!", ip);
}

static void call(bt_Context* context, bt_Thread* thread, bt_Module* module, bt_Op* ip, bt_Value* constants, int8_t return_loc)
{
	register bt_Value* stack = thread->stack + thread->top;
	_mm_prefetch(stack, 1);
	register bt_Value* upv = thread->callstack[thread->depth - 1].upvals;
	register bt_Op op;

#define NEXT break;
#define RETURN return;
	for (;;) {
		op = *ip++;
		switch (BT_GET_OPCODE(op)) {
		case BT_OP_LOAD:        stack[BT_GET_A(op)] = constants[BT_GET_B(op)];                       NEXT;
		case BT_OP_LOAD_SMALL:  stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_GET_IBC(op));               NEXT;
		case BT_OP_LOAD_NULL:   stack[BT_GET_A(op)] = BT_VALUE_NULL;                                 NEXT;
		case BT_OP_LOAD_BOOL:   stack[BT_GET_A(op)] = BT_GET_B(op) ? BT_VALUE_TRUE : BT_VALUE_FALSE; NEXT;
		case BT_OP_LOAD_IMPORT: stack[BT_GET_A(op)] = module->imports.elements[BT_GET_B(op)]->value; NEXT;

		case BT_OP_TABLE: stack[BT_GET_A(op)] = BT_VALUE_OBJECT(bt_make_table(context, BT_GET_IBC(op))); NEXT;
		case BT_OP_TTABLE: {
			bt_Table* tbl = bt_make_table(context, BT_GET_B(op));
			tbl->prototype = bt_type_get_proto(context, BT_AS_OBJECT(stack[BT_GET_C(op)]));
			stack[BT_GET_A(op)] = BT_VALUE_OBJECT(tbl);
		} NEXT;

		case BT_OP_ARRAY: {
			bt_Array* arr = bt_make_array(context, BT_GET_IBC(op));
			arr->items.length = BT_GET_IBC(op);
			stack[BT_GET_A(op)] = BT_VALUE_OBJECT(arr);
		} NEXT;

		case BT_OP_MOVE: stack[BT_GET_A(op)] = stack[BT_GET_B(op)]; NEXT;

		case BT_OP_EXPORT: {
			bt_module_export(context, module, BT_AS_OBJECT(stack[BT_GET_C(op)]), stack[BT_GET_A(op)], stack[BT_GET_B(op)]);
		} NEXT;

		case BT_OP_CLOSE: {
			bt_ValueBuffer upvals;
			bt_buffer_with_capacity(&upvals, context, BT_GET_C(op));
			bt_Object* obj = BT_AS_OBJECT(stack[BT_GET_B(op)]);
			for (uint8_t i = 0; i < BT_GET_C(op); i++) {
				bt_buffer_push(context, &upvals, stack[BT_GET_B(op) + 1 + i]);
			}
			bt_Closure* cl = BT_ALLOCATE(context, CLOSURE, bt_Closure);
			cl->fn = obj;
			cl->upvals = upvals;
			stack[BT_GET_A(op)] = BT_VALUE_OBJECT(cl);
		} NEXT;

		case BT_OP_LOADUP: BT_ASSUME(upv); stack[BT_GET_A(op)] = upv[BT_GET_B(op)];  NEXT;
		case BT_OP_STOREUP: BT_ASSUME(upv); upv[BT_GET_A(op)] = stack[BT_GET_B(op)]; NEXT;

		case BT_OP_NEG: bt_neg(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], ip);                      NEXT;
		case BT_OP_ADD: bt_add(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); NEXT;
		case BT_OP_SUB: bt_sub(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); NEXT;
		case BT_OP_MUL: bt_mul(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); NEXT;
		case BT_OP_DIV: bt_div(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); NEXT;

		case BT_OP_EQ:  bt_eq(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip);  NEXT;
		case BT_OP_NEQ: bt_neq(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); NEXT;
		case BT_OP_LT:  bt_lt(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip);  NEXT;
		case BT_OP_LTE: bt_lte(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); NEXT;

		case BT_OP_AND: bt_and(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); NEXT;
		case BT_OP_OR:  bt_or(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip);  NEXT;
		case BT_OP_NOT: bt_not(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], ip);                      NEXT;

		case BT_OP_LOAD_IDX: stack[BT_GET_A(op)] = bt_get(context, BT_AS_OBJECT(stack[BT_GET_B(op)]), stack[BT_GET_C(op)]); NEXT;
		case BT_OP_STORE_IDX: bt_set(context, BT_AS_OBJECT(stack[BT_GET_A(op)]), stack[BT_GET_B(op)], stack[BT_GET_C(op)]); NEXT;

		case BT_OP_LOAD_IDX_K: stack[BT_GET_A(op)] = bt_get(context, BT_AS_OBJECT(stack[BT_GET_B(op)]), constants[BT_GET_C(op)]); NEXT;
		case BT_OP_STORE_IDX_K: bt_set(context, BT_AS_OBJECT(stack[BT_GET_A(op)]), constants[BT_GET_B(op)], stack[BT_GET_C(op)]); NEXT;

		case BT_OP_LOAD_IDX_F: stack[BT_GET_A(op)] = ((bt_Table*)BT_AS_OBJECT(stack[BT_GET_B(op)]))->pairs.elements[BT_GET_C(op)].value; NEXT;
		case BT_OP_STORE_IDX_F: ((bt_Table*)BT_AS_OBJECT(stack[BT_GET_B(op)]))->pairs.elements[BT_GET_C(op)].value = stack[BT_GET_C(op)]; NEXT;

		case BT_OP_EXPECT:   stack[BT_GET_A(op)] = stack[BT_GET_B(op)]; if (stack[BT_GET_A(op)] == BT_VALUE_NULL) bt_runtime_error(thread, "Operator '!' failed - lhs was null!", ip); NEXT;
		case BT_OP_EXISTS:   stack[BT_GET_A(op)] = stack[BT_GET_B(op)] == BT_VALUE_NULL ? BT_VALUE_FALSE : BT_VALUE_TRUE; NEXT;
		case BT_OP_COALESCE: stack[BT_GET_A(op)] = stack[BT_GET_B(op)] == BT_VALUE_NULL ? stack[BT_GET_C(op)] : stack[BT_GET_B(op)];   NEXT;

		case BT_OP_TCHECK: {
			stack[BT_GET_A(op)] = bt_is_type(stack[BT_GET_B(op)], BT_AS_OBJECT(stack[BT_GET_C(op)])) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		} NEXT;

		case BT_OP_TSATIS: {
			stack[BT_GET_A(op)] = bt_satisfies_type(stack[BT_GET_B(op)], BT_AS_OBJECT(stack[BT_GET_C(op)])) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		} NEXT;

		case BT_OP_TCAST: {
			stack[BT_GET_A(op)] = bt_cast_type(stack[BT_GET_B(op)], BT_AS_OBJECT(stack[BT_GET_C(op)]));
		} NEXT;

		case BT_OP_TALIAS: {
			bt_Table* tbl = BT_AS_OBJECT(stack[BT_GET_B(op)]);
			tbl->prototype = bt_type_get_proto(context, BT_AS_OBJECT(stack[BT_GET_C(op)]));
			stack[BT_GET_A(op)] = stack[BT_GET_B(op)];
		} NEXT;

		case BT_OP_COMPOSE: {
			bt_Table* lhs = BT_AS_OBJECT(stack[BT_GET_B(op)]);
			bt_Table* rhs = BT_AS_OBJECT(stack[BT_GET_C(op)]);
			bt_Table* result = bt_make_table(context, lhs->pairs.length + rhs->pairs.length);
			bt_buffer_append(context, &result->pairs, &lhs->pairs);
			bt_buffer_append(context, &result->pairs, &rhs->pairs);
			stack[BT_GET_A(op)] = BT_VALUE_OBJECT(result);
		} NEXT;

		case BT_OP_CALL: {
			uint16_t old_top = thread->top;

			bt_Object* obj = BT_AS_OBJECT(stack[BT_GET_B(op)]);

			thread->top += BT_GET_B(op) + 1;
			thread->callstack[thread->depth].return_loc = BT_GET_A(op) - (BT_GET_B(op) + 1);
			thread->callstack[thread->depth].argc = BT_GET_C(op);
			thread->callstack[thread->depth].callable = obj;
			thread->depth++;

			uint32_t type = BT_OBJECT_GET_TYPE(obj);
			if (type == BT_OBJECT_TYPE_FN) {
				bt_Fn* callable = (bt_Fn*)obj;
				thread->callstack[thread->depth - 1].size = callable->stack_size;
				call(context, thread, callable->module, callable->instructions.elements, callable->constants.elements, BT_GET_A(op) - (BT_GET_B(op) + 1));
			}
			else if (type == BT_OBJECT_TYPE_CLOSURE) {
				bt_Fn* callable = ((bt_Closure*)obj)->fn;
				thread->callstack[thread->depth - 1].size = callable->stack_size;
				thread->callstack[thread->depth - 1].upvals = ((bt_Closure*)obj)->upvals.elements;

				if (BT_OBJECT_GET_TYPE(callable) == BT_OBJECT_TYPE_FN) {
					call(context, thread, callable->module, callable->instructions.elements, callable->constants.elements, BT_GET_A(op) - (BT_GET_B(op) + 1));
				}
				else if (BT_OBJECT_GET_TYPE(callable) == BT_OBJECT_TYPE_NATIVE_FN) {
					thread->callstack[thread->depth - 1].size = 0;
					thread->callstack[thread->depth - 1].user_top = 0;
					bt_NativeFn* as_native = callable;
					as_native->fn(context, thread);
				}
				else {
					bt_runtime_error(thread, "Closure contained unsupported callable type.", ip);
				}
			}
			else if (type == BT_OBJECT_TYPE_NATIVE_FN) {
				thread->callstack[thread->depth - 1].size = 0;
				thread->callstack[thread->depth - 1].user_top = 0;
				bt_NativeFn* callable = (bt_NativeFn*)obj;
				callable->fn(context, thread);
			}
			else {
				bt_Type* as_type = obj;
				bt_runtime_error(thread, "Unsupported callable type.", ip);
			}

			thread->depth--;
			thread->top = old_top;
		} NEXT;

		case BT_OP_JMP: ip += BT_GET_IBC(op); NEXT;
		case BT_OP_JMPF: if (stack[BT_GET_A(op)] == BT_VALUE_FALSE) ip += BT_GET_IBC(op); NEXT;

		case BT_OP_RETURN: stack[return_loc] = stack[BT_GET_A(op)]; RETURN;
		case BT_OP_END: RETURN;

		case BT_OP_NUMFOR: {
			stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_A(op)]) + BT_AS_NUMBER(stack[BT_GET_A(op) + 1]));
			if (BT_AS_NUMBER(stack[BT_GET_A(op)]) >= BT_AS_NUMBER(stack[BT_GET_A(op) + 2])) ip += BT_GET_IBC(op);
		} NEXT;

		case BT_OP_ITERFOR: {
			bt_Closure* cl = BT_AS_OBJECT(stack[BT_GET_A(op) + 1]);
			BT_ASSUME(cl);
			thread->top += BT_GET_A(op) + 2;
			thread->callstack[thread->depth].upvals = cl->upvals.elements;
			thread->depth++;

			if (BT_OBJECT_GET_TYPE(cl->fn) == BT_OBJECT_TYPE_FN) {
				call(context, thread, cl->fn->module, cl->fn->instructions.elements, cl->fn->constants.elements, BT_GET_A(op) - thread->top);
			}
			else {
				((bt_NativeFn*)cl->fn)->fn(context, thread);
			}

			thread->depth--;
			thread->top -= BT_GET_A(op) + 2;
			if (stack[BT_GET_A(op)] == BT_VALUE_NULL) { ip += BT_GET_IBC(op); }
		} NEXT;
		case BT_OP_ADDF: {
			stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) + BT_AS_NUMBER(stack[BT_GET_C(op)]));
		} NEXT;
		case BT_OP_SUBF: {
			stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) - BT_AS_NUMBER(stack[BT_GET_C(op)]));
		} NEXT;
		case BT_OP_MULF: {
			stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) * BT_AS_NUMBER(stack[BT_GET_C(op)]));
		} NEXT;
		case BT_OP_DIVF: {
			stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) / BT_AS_NUMBER(stack[BT_GET_C(op)]));
		} NEXT;
		case BT_OP_LTF:  stack[BT_GET_A(op)] = BT_VALUE_FALSE + (BT_AS_NUMBER(stack[BT_GET_B(op)]) < BT_AS_NUMBER(stack[BT_GET_C(op)])); NEXT;
		case BT_OP_LOAD_SUB_F: {
			stack[BT_GET_A(op)] = bt_array_get(context, BT_AS_OBJECT(stack[BT_GET_B(op)]), BT_AS_NUMBER(stack[BT_GET_C(op)]));
		} NEXT;
		default: BT_ASSUME(0);
		}
	}
}