#include "bolt.h"

#ifdef BT_DEBUG
#include <assert.h>
#endif 

#include <memory.h>
#include <immintrin.h>
#include <string.h>

#include "bt_object.h"
#include "bt_tokenizer.h"
#include "bt_parser.h"
#include "bt_compiler.h"
#include "bt_debug.h"
#include "bt_gc.h"

static bt_Type* make_primitive_type(bt_Context* ctx, const char* name, bt_TypeSatisfier satisfier)
{
	return bt_make_type(ctx, name, satisfier, BT_TYPE_CATEGORY_PRIMITIVE);
}

void bt_open(bt_Context* context, bt_Handlers* handlers)
{
	context->alloc = handlers->alloc;
	context->free = handlers->free;
	context->realloc = handlers->realloc;
	context->on_error = handlers->on_error;

	context->read_file = handlers->read_file;
	context->close_file = handlers->close_file;
	context->free_source = handlers->free_source;

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
	context->types.table = 0;
	context->types.table = bt_make_tableshape(context, "table", BT_FALSE);

	context->types.any = make_primitive_type(context, "any", bt_type_satisfier_any);
	context->types.null = make_primitive_type(context, "null", bt_type_satisfier_null);
	
	context->types.array = 0;
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
	context->meta_names.lt = bt_make_string_hashed_len(context, "@lt", 3);
	context->meta_names.lte = bt_make_string_hashed_len(context, "@lte", 4);
	context->meta_names.eq = bt_make_string_hashed_len(context, "@eq", 3);
	context->meta_names.neq = bt_make_string_hashed_len(context, "@neq", 4);
	context->meta_names.format = bt_make_string_hashed_len(context, "@format", 7);
	context->meta_names.collect = bt_make_string_hashed_len(context, "@collect", 8);

	context->compiler_options.generate_debug_info = BT_TRUE;

	context->module_paths = NULL;
	bt_append_module_path(context, "%s.bolt");
	bt_append_module_path(context, "%s/module.bolt");

	context->is_valid = BT_TRUE;
}

#ifdef BOLT_ALLOW_PRINTF
#include <stdio.h>

static void bt_error(bt_ErrorType type, const char* module, const char* message, uint16_t line, uint16_t col) {
	switch (type)
	{
	case BT_ERROR_PARSE: {
		printf("parse error [%s (%d:%d)]: %s\n", module, line, col, message);
	} break;

	case BT_ERROR_COMPILE: {
		printf("compile error [%s (%d:%d)]: %s\n", module, line, col, message);
	} break;

	case BT_ERROR_RUNTIME: {
		printf("runtime error [%s (%d:%d)]: %s\n", module, line, col, message);
	} break;
	}
}
#endif

#ifdef BOLT_ALLOW_MALLOC
#include <malloc.h>
#endif

#ifdef BOLT_ALLOW_FOPEN
#include <stdio.h>

static char* bt_read_file(bt_Context* ctx, const char* path, void** handle)
{
	fopen_s((FILE**)handle, path, "rb");

	if (*handle == 0) return NULL;

	fseek(*handle, 0, SEEK_END);
	uint32_t len = ftell(*handle);
	fseek(*handle, 0, SEEK_SET);

	char* code = ctx->alloc(len + 1);
	fread(code, 1, len, *handle);
	code[len] = 0;

	return code;
}

static void bt_close_file(bt_Context* ctx, const char* path, void* handle)
{
	fclose(handle);
}

static void bt_free_source(bt_Context* ctx, char* source)
{
	ctx->free(source);
}
#endif

bt_Handlers bt_default_handlers()
{
	bt_Handlers result = { 0 };

#ifdef BOLT_ALLOW_MALLOC
	result.alloc = malloc;
	result.realloc = realloc;
	result.free = free;
#endif

#ifdef BOLT_ALLOW_FOPEN
	result.read_file = bt_read_file;
	result.close_file = bt_close_file;
	result.free_source = bt_free_source;
#endif

#ifdef BOLT_ALLOW_PRINTF
	result.on_error = bt_error;
#endif

	return result;
}

void bt_close(bt_Context* context)
{
	bt_Object* obj = context->root;

	while (obj) {
		bt_Object* next = (bt_Object*)BT_OBJECT_NEXT(obj);
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
	bt_Module* mod = bt_compile_module(context, source, "<interp>");
	if (!mod) return BT_FALSE;
	return bt_execute(context, mod);
}

bt_Module* bt_compile_module(bt_Context* context, const char* source, const char* mod_name)
{
#ifdef BOLT_PRINT_DEBUG
	printf("%s\n", source);
	printf("-----------------------------------------------------\n");
#endif

	bt_Tokenizer* tok = context->alloc(sizeof(bt_Tokenizer));
	*tok = bt_open_tokenizer(context);
	bt_tokenizer_set_source(tok, source);
	bt_tokenizer_set_source_name(tok, mod_name);

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
		bt_Type* type = (bt_Type*)obj;
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
	case BT_OBJECT_TYPE_MODULE: {
		bt_Module* mod = (bt_Module*)obj;
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
		bt_Fn* fn = (bt_Fn*)obj;
		bt_buffer_destroy(context, &fn->constants);
		bt_buffer_destroy(context, &fn->instructions);
		if (fn->debug) {
			bt_buffer_destroy(context, fn->debug);
			context->free(fn->debug);
		}
	} break;
	case BT_OBJECT_TYPE_TABLE: {
		bt_Table* tbl = (bt_Table*)obj;
		if (!tbl->is_inline) {
			context->gc.byets_allocated -= tbl->capacity * sizeof(bt_TablePair);
			context->free(tbl->outline);
		}
	} break;
	case BT_OBJECT_TYPE_ARRAY: {
		bt_Array* arr = (bt_Array*)obj;
		bt_buffer_destroy(context, &arr->items);
	} break;
	case BT_OBJECT_TYPE_USERDATA: {
		bt_Userdata* userdata = (bt_Userdata*)obj;
		context->free(userdata->data);
	} break;
	}
}

static uint32_t get_object_size(bt_Object* obj)
{
	switch (BT_OBJECT_GET_TYPE(obj)) {
	case BT_OBJECT_TYPE_NONE: return sizeof(bt_Object);
	case BT_OBJECT_TYPE_TYPE: return sizeof(bt_Type);
	case BT_OBJECT_TYPE_STRING: return sizeof(bt_String) + ((bt_String*)obj)->len;
	case BT_OBJECT_TYPE_MODULE: return sizeof(bt_Module);
	case BT_OBJECT_TYPE_IMPORT: return sizeof(bt_ModuleImport);
	case BT_OBJECT_TYPE_FN: return sizeof(bt_Fn);
	case BT_OBJECT_TYPE_NATIVE_FN: return sizeof(bt_NativeFn);
	case BT_OBJECT_TYPE_CLOSURE: return sizeof(bt_Closure) + ((bt_Closure*)obj)->num_upv * sizeof(bt_Value);
	case BT_OBJECT_TYPE_METHOD: return sizeof(bt_Fn);
	case BT_OBJECT_TYPE_ARRAY: return sizeof(bt_Array);
	case BT_OBJECT_TYPE_TABLE: return sizeof(bt_Table) + sizeof(bt_TablePair) * ((bt_Table*)obj)->inline_capacity;
	case BT_OBJECT_TYPE_USERDATA: return sizeof(bt_Userdata);
	}

#ifdef BT_DEBUG
	assert(0 && "Attempted to get size of unrecognized type!");
#endif
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
	new_import->name = (bt_String*)BT_AS_OBJECT(name);
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
	bt_Module* mod = (bt_Module*)BT_AS_OBJECT(bt_table_get(context->loaded_modules, name));
	if (mod == 0) {
		bt_String* to_load = (bt_String*)BT_AS_OBJECT(name);

		char path_buf[256];
		uint32_t path_len = 0;
		void* handle = NULL;
		char* code = NULL;

		bt_Path* pathspec = context->module_paths;
		while (pathspec && !code) {
			path_len = sprintf_s(path_buf, 256, pathspec->spec, BT_STRING_STR(to_load));

			if (path_len >= 256) {
				bt_runtime_error(context->current_thread, "Path buffer overrun when loading module!", NULL);
				return NULL;
			}

			path_buf[path_len] = 0;
			code = context->read_file(context, path_buf, &handle);

			pathspec = pathspec->next;
		}

		if (code == 0) {
			if(context->current_thread) bt_runtime_error(context->current_thread, "Cannot find module file", NULL);
			return NULL;
		}

		context->close_file(context, path_buf, handle);

		bt_Module* new_mod = bt_compile_module(context, code, path_buf);
		context->free_source(context, code);

		if (new_mod) {
			new_mod->name = (bt_String*)BT_AS_OBJECT(name);
			new_mod->path = bt_make_string_len(context, path_buf, path_len);
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
	thread->callstack[thread->depth].callable = (bt_Callable*)module;
	thread->callstack[thread->depth].user_top = 0;
	thread->depth++;

	context->current_thread = thread;

	int32_t result = setjmp(&thread->error_loc[0]);

	if (result == 0) call(context, thread, module, module->instructions.elements, module->constants.elements, 0);
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
	default: bt_runtime_error(NULL, "Failed to get module from callable", NULL); return NULL;
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
		// TODO(bearish): use this for something, better formatted errors
		//bt_StrSlice line_source = bt_get_debug_line(source, source_token->line - 1);
		bt_Module* module = get_module(callable);

		thread->context->on_error(BT_ERROR_RUNTIME, (module && module->path) ? BT_STRING_STR(module->path) : "", message, source_token->line - 1, source_token->col);
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

	bt_Closure* cl = BT_ALLOCATE_INLINE_STORAGE(thread->context, CLOSURE, bt_Closure, sizeof(bt_Value) * num_upvals);
	cl->num_upv = num_upvals;
	bt_Value* upv = BT_CLOSURE_UPVALS(cl);
	for (bt_Value* top = true_top - (num_upvals - 1); top <= true_top; top++) {
		*upv = *top;
		upv++;
	}

	cl->fn = (bt_Fn*)BT_AS_OBJECT(*(true_top - num_upvals));
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
	thread->callstack[thread->depth].callable = (bt_Callable*)obj;
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
			return (bt_StrSlice) { line_start, (uint16_t)(source - line_start) };
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
		return (uint32_t)(ip - instructions->elements);
	}

	return 0;
}

#define XSTR(x) #x
#define ARITH_MF(name)                                                                               \
if (BT_IS_OBJECT(lhs)) {																			 \
	bt_Object* obj = BT_AS_OBJECT(lhs);																 \
	if (BT_OBJECT_GET_TYPE(obj) == BT_OBJECT_TYPE_TABLE) {											 \
		bt_Table* tbl = (bt_Table*)obj;																 \
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
		bt_String* lhs_str = (bt_String*)BT_AS_OBJECT(lhs);
		bt_String* rhs_str = (bt_String*)BT_AS_OBJECT(rhs);

		if (BT_OBJECT_GET_TYPE(lhs_str) == BT_OBJECT_TYPE_STRING && BT_OBJECT_GET_TYPE(rhs_str) == BT_OBJECT_TYPE_STRING) {
			uint32_t length = lhs_str->len + rhs_str->len;

			bt_String* str_result = bt_make_string_empty(thread->context, length);
			char* added = BT_STRING_STR(str_result);
			memcpy(added, BT_STRING_STR(lhs_str), lhs_str->len);
			memcpy(added + lhs_str->len, BT_STRING_STR(rhs_str), rhs_str->len);
			added[length] = 0;

			*result = BT_VALUE_OBJECT(str_result);
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
	_mm_prefetch((const char*)stack, 1);
	register bt_Value* upv = BT_CLOSURE_UPVALS(thread->callstack[thread->depth - 1].callable);
	register bt_Object* obj, *obj2;

	BT_ASSUME(obj);
	BT_ASSUME(obj2);

#ifndef BOLT_USE_INLINE_THREADING
	register bt_Op op;
#define NEXT break;
#define RETURN return;
#define CASE(x) case BT_OP_##x
#define DISPATCH \
	op = *ip++; \
	switch(BT_GET_OPCODE(op))
#else
#define RETURN return;
#define CASE(x) lbl_##x
#define X(op) case BT_OP_##op: goto lbl_##op;
#define op (*ip)
#define NEXT                          \
	switch (BT_GET_OPCODE(*(++ip))) { \
		BT_OPS_X                      \
	}
#define DISPATCH                      \
	switch (BT_GET_OPCODE(op)) {	  \
		BT_OPS_X                      \
	}
#endif
#ifndef BOLT_USE_INLINE_THREADING
	for (;;) 
#endif 
	{
		DISPATCH {
		CASE(LOAD):        stack[BT_GET_A(op)] = constants[BT_GET_B(op)];                       NEXT;
		CASE(LOAD_SMALL):  stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_GET_IBC(op));               NEXT;
		CASE(LOAD_NULL):   stack[BT_GET_A(op)] = BT_VALUE_NULL;                                 NEXT;
		CASE(LOAD_BOOL):   stack[BT_GET_A(op)] = BT_GET_B(op) ? BT_VALUE_TRUE : BT_VALUE_FALSE; NEXT;
		CASE(LOAD_IMPORT): stack[BT_GET_A(op)] = module->imports.elements[BT_GET_B(op)]->value; NEXT;

		CASE(TABLE): 
			if (BT_IS_ACCELERATED(op)) {
				obj = (bt_Object*)bt_make_table(context, BT_GET_B(op));
				((bt_Table*)obj)->prototype = bt_type_get_proto(context, (bt_Type*)BT_AS_OBJECT(stack[BT_GET_C(op)]));
				stack[BT_GET_A(op)] = BT_VALUE_OBJECT(obj);
			}
			else stack[BT_GET_A(op)] = BT_VALUE_OBJECT(bt_make_table(context, BT_GET_IBC(op))); 
		NEXT;

		CASE(ARRAY):
			obj = (bt_Object*)bt_make_array(context, BT_GET_IBC(op));
			((bt_Array*)obj)->items.length = BT_GET_IBC(op);
			stack[BT_GET_A(op)] = BT_VALUE_OBJECT(obj);
		NEXT;

		CASE(MOVE): stack[BT_GET_A(op)] = stack[BT_GET_B(op)]; NEXT;

		CASE(EXPORT): bt_module_export(context, module, (bt_Type*)BT_AS_OBJECT(stack[BT_GET_C(op)]), stack[BT_GET_A(op)], stack[BT_GET_B(op)]); NEXT;

		CASE(CLOSE):
			obj2 = (bt_Object*)BT_ALLOCATE_INLINE_STORAGE(context, CLOSURE, bt_Closure, sizeof(bt_Value) * BT_GET_C(op));
			obj = BT_AS_OBJECT(stack[BT_GET_B(op)]);
			for (uint8_t i = 0; i < BT_GET_C(op); i++) {
				BT_CLOSURE_UPVALS(obj2)[i] = stack[BT_GET_B(op) + 1 + i];
			}
			((bt_Closure*)obj2)->fn = (bt_Fn*)obj;
			((bt_Closure*)obj2)->num_upv = BT_GET_C(op);
			stack[BT_GET_A(op)] = BT_VALUE_OBJECT(obj2);
		NEXT;

		CASE(LOADUP):  BT_ASSUME(upv); stack[BT_GET_A(op)] = upv[BT_GET_B(op)]; NEXT;
		CASE(STOREUP): BT_ASSUME(upv); upv[BT_GET_A(op)] = stack[BT_GET_B(op)]; NEXT;

		CASE(NEG):
			if(BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_NUMBER(-BT_AS_NUMBER(stack[BT_GET_B(op)]));
			else bt_neg(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], ip);                      
		NEXT;
		
		CASE(ADD): 
			if(BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) + BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else bt_add(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); 
		NEXT;
		
		CASE(SUB): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) - BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else bt_sub(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); 
		NEXT;

		CASE(MUL): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) * BT_AS_NUMBER(stack[BT_GET_C(op)])); 
			else bt_mul(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); 
		NEXT;

		CASE(DIV): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) / BT_AS_NUMBER(stack[BT_GET_C(op)])); 
			else bt_div(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); 
		NEXT;

		CASE(EQ):
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_FALSE + (BT_AS_NUMBER(stack[BT_GET_B(op)]) == BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else bt_eq(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip);  
		NEXT;

		CASE(NEQ): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_FALSE + (BT_AS_NUMBER(stack[BT_GET_B(op)]) != BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else bt_neq(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip);
		NEXT;
		
		CASE(LT): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_FALSE + (BT_AS_NUMBER(stack[BT_GET_B(op)]) < BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else bt_lt(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip);
		NEXT;

		CASE(LTE):
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_FALSE + (BT_AS_NUMBER(stack[BT_GET_B(op)]) <= BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else bt_lte(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip);
		NEXT;

		CASE(AND): bt_and(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); NEXT;
		CASE(OR):  bt_or(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip);  NEXT;
		CASE(NOT): bt_not(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], ip);                      NEXT;

		CASE(LOAD_IDX): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = (BT_TABLE_PAIRS(BT_AS_OBJECT(stack[BT_GET_B(op)])) + BT_GET_C(op))->value;
			else stack[BT_GET_A(op)] = bt_get(context, BT_AS_OBJECT(stack[BT_GET_B(op)]), stack[BT_GET_C(op)]); 
		NEXT;

		CASE(STORE_IDX): 
			if (BT_IS_ACCELERATED(op)) (BT_TABLE_PAIRS(BT_AS_OBJECT(stack[BT_GET_A(op)])) + BT_GET_B(op))->value = stack[BT_GET_C(op)]; 
			else bt_set(context, BT_AS_OBJECT(stack[BT_GET_A(op)]), stack[BT_GET_B(op)], stack[BT_GET_C(op)]); 
		NEXT;

		CASE(LOAD_IDX_K): 
			stack[BT_GET_A(op)] = bt_get(context, BT_AS_OBJECT(stack[BT_GET_B(op)]), constants[BT_GET_C(op)]); 
		NEXT;
		CASE(STORE_IDX_K): bt_set(context, BT_AS_OBJECT(stack[BT_GET_A(op)]), constants[BT_GET_B(op)], stack[BT_GET_C(op)]); NEXT;

		CASE(EXPECT):   stack[BT_GET_A(op)] = stack[BT_GET_B(op)]; if (stack[BT_GET_A(op)] == BT_VALUE_NULL) bt_runtime_error(thread, "Operator '!' failed - lhs was null!", ip); NEXT;
		CASE(EXISTS):   stack[BT_GET_A(op)] = stack[BT_GET_B(op)] == BT_VALUE_NULL ? BT_VALUE_FALSE : BT_VALUE_TRUE; NEXT;
		CASE(COALESCE): stack[BT_GET_A(op)] = stack[BT_GET_B(op)] == BT_VALUE_NULL ? stack[BT_GET_C(op)] : stack[BT_GET_B(op)];   NEXT;

		CASE(TCHECK): stack[BT_GET_A(op)] = bt_is_type(stack[BT_GET_B(op)], (bt_Type*)BT_AS_OBJECT(stack[BT_GET_C(op)])) ? BT_VALUE_TRUE : BT_VALUE_FALSE; NEXT;
		CASE(TSATIS): stack[BT_GET_A(op)] = bt_satisfies_type(stack[BT_GET_B(op)], (bt_Type*)BT_AS_OBJECT(stack[BT_GET_C(op)])) ? BT_VALUE_TRUE : BT_VALUE_FALSE; NEXT;
		CASE(TCAST): 
			if (BT_IS_ACCELERATED(op)) {
				obj = BT_AS_OBJECT(stack[BT_GET_B(op)]);
				((bt_Table*)obj)->prototype = bt_type_get_proto(context, (bt_Type*)BT_AS_OBJECT(stack[BT_GET_C(op)]));
				stack[BT_GET_A(op)] = stack[BT_GET_B(op)];
			}
			else stack[BT_GET_A(op)] = bt_cast_type(stack[BT_GET_B(op)], (bt_Type*)BT_AS_OBJECT(stack[BT_GET_C(op)])); 
		NEXT;

		CASE(TSET):
			bt_type_set_field(context, (bt_Type*)BT_AS_OBJECT(stack[BT_GET_A(op)]), stack[BT_GET_B(op)], stack[BT_GET_C(op)]);
		NEXT;

		CASE(COMPOSE):
			obj  = BT_AS_OBJECT(stack[BT_GET_B(op)]);
			obj2 = BT_AS_OBJECT(stack[BT_GET_C(op)]);
			stack[BT_GET_A(op)] = BT_VALUE_OBJECT(bt_make_table(context, ((bt_Table*)obj)->length + ((bt_Table*)obj2)->length));
			for (uint32_t i = 0; i < ((bt_Table*)obj)->length; ++i) *(BT_TABLE_PAIRS(BT_AS_OBJECT(stack[BT_GET_A(op)])) + i) = *(BT_TABLE_PAIRS(obj) + i);
			for (uint32_t i = 0; i < ((bt_Table*)obj2)->length; ++i) *(BT_TABLE_PAIRS(BT_AS_OBJECT(stack[BT_GET_A(op)])) + i + ((bt_Table*)obj)->length) = *(BT_TABLE_PAIRS(obj2) + i);
		NEXT;

		CASE(CALL):
			obj2 = (bt_Object*)(uint64_t)thread->top;

			obj = BT_AS_OBJECT(stack[BT_GET_B(op)]);

			thread->top += BT_GET_B(op) + 1;
			thread->callstack[thread->depth].return_loc = BT_GET_A(op) - (BT_GET_B(op) + 1);
			thread->callstack[thread->depth].argc = BT_GET_C(op);
			thread->callstack[thread->depth].callable = (bt_Callable*)obj;
			thread->depth++;

			switch (BT_OBJECT_GET_TYPE(obj)) {
			case BT_OBJECT_TYPE_FN:
				thread->callstack[thread->depth - 1].size = ((bt_Fn*)obj)->stack_size;
				call(context, thread, ((bt_Fn*)obj)->module, ((bt_Fn*)obj)->instructions.elements, ((bt_Fn*)obj)->constants.elements, BT_GET_A(op) - (BT_GET_B(op) + 1));
			break;
			case BT_OBJECT_TYPE_CLOSURE:
				switch (BT_OBJECT_GET_TYPE(((bt_Closure*)obj)->fn)) {
				case BT_OBJECT_TYPE_FN:
					thread->callstack[thread->depth - 1].size = ((bt_Closure*)obj)->fn->stack_size;
					call(context, thread, ((bt_Closure*)obj)->fn->module, ((bt_Closure*)obj)->fn->instructions.elements, ((bt_Closure*)obj)->fn->constants.elements, BT_GET_A(op) - (BT_GET_B(op) + 1));
					break;
				case BT_OBJECT_TYPE_NATIVE_FN:
					thread->callstack[thread->depth - 1].size = 0;
					thread->callstack[thread->depth - 1].user_top = 0;
					((bt_NativeFn*)((bt_Closure*)obj)->fn)->fn(context, thread);
					break;
				default: bt_runtime_error(thread, "Closure contained unsupported callable type.", ip);
				}
			break;
			case BT_OBJECT_TYPE_NATIVE_FN:
				thread->callstack[thread->depth - 1].size = 0;
				thread->callstack[thread->depth - 1].user_top = 0;
				((bt_NativeFn*)obj)->fn(context, thread);
			break;
			default: bt_runtime_error(thread, "Unsupported callable type.", ip);
			}

			thread->depth--;
			thread->top = (uint32_t)(uint64_t)obj2;
		NEXT;

		CASE(JMP): ip += BT_GET_IBC(op); NEXT;
		CASE(JMPF): if (stack[BT_GET_A(op)] == BT_VALUE_FALSE) ip += BT_GET_IBC(op); NEXT;

		CASE(RETURN): stack[return_loc] = stack[BT_GET_A(op)];
		CASE(END): RETURN;

		CASE(NUMFOR):
			stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_A(op)]) + BT_AS_NUMBER(stack[BT_GET_A(op) + 1]));
			if (BT_AS_NUMBER(stack[BT_GET_A(op)]) >= BT_AS_NUMBER(stack[BT_GET_A(op) + 2])) ip += BT_GET_IBC(op);
		NEXT;

		CASE(ITERFOR):
			obj = BT_AS_OBJECT(stack[BT_GET_A(op) + 1]);
			thread->top += BT_GET_A(op) + 2;
			thread->callstack[thread->depth].callable = (bt_Callable*)obj;
			thread->depth++;

			if (BT_OBJECT_GET_TYPE(((bt_Closure*)obj)->fn) == BT_OBJECT_TYPE_FN) {
				call(context, thread, ((bt_Closure*)obj)->fn->module, ((bt_Closure*)obj)->fn->instructions.elements, ((bt_Closure*)obj)->fn->constants.elements, -2);
			}
			else {
				((bt_NativeFn*)((bt_Closure*)obj)->fn)->fn(context, thread);
			}

			thread->depth--;
			thread->top -= BT_GET_A(op) + 2;
			if (stack[BT_GET_A(op)] == BT_VALUE_NULL) { ip += BT_GET_IBC(op); }
		NEXT;

		CASE(LOAD_SUB_F): stack[BT_GET_A(op)] = bt_array_get(context, (bt_Array*)BT_AS_OBJECT(stack[BT_GET_B(op)]), (uint64_t)BT_AS_NUMBER(stack[BT_GET_C(op)])); NEXT;
		CASE(STORE_SUB_F): bt_array_set(context, (bt_Array*)BT_AS_OBJECT(stack[BT_GET_A(op)]), (uint64_t)BT_AS_NUMBER(stack[BT_GET_B(op)]), stack[BT_GET_C(op)]); NEXT;
#ifndef BOLT_USE_INLINE_THREADING
#ifdef BT_DEBUG
		default: assert(0 && "Unimplemented opcode!");
#else
		default: BT_ASSUME(0);
#endif
#endif
		}
	}
}