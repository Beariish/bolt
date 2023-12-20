#include "boltstd_meta.h"

#include "../bt_embedding.h"
#include "../bt_type.h"
#include "../bt_debug.h"

static void btstd_gc(bt_Context* ctx, bt_Thread* thread)
{
	uint32_t n_collected = bt_collect(&ctx->gc, 0);
	bt_return(thread, BT_VALUE_NUMBER(n_collected));
}

static void btstd_memsize(bt_Context* ctx, bt_Thread* thread)
{
	bt_return(thread, bt_make_number((bt_number)ctx->gc.byets_allocated));
}

static void btstd_nextcycle(bt_Context* ctx, bt_Thread* thread)
{
	bt_return(thread, bt_make_number((bt_number)ctx->gc.next_cycle));
}

static void btstd_grey(bt_Context* ctx, bt_Thread* thread)
{
	if (!BT_IS_OBJECT(bt_arg(thread, 0))) return;

	bt_Object* arg = BT_AS_OBJECT(bt_arg(thread, 0));
	bt_grey_obj(ctx, arg);
}

static void btstd_add_reference(bt_Context* ctx, bt_Thread* thread)
{
	if (!BT_IS_OBJECT(bt_arg(thread, 0))) return;

	bt_Object* arg = BT_AS_OBJECT(bt_arg(thread, 0));
	bt_return(thread, BT_VALUE_NUMBER(bt_add_ref(ctx, arg)));
}

static void btstd_remove_reference(bt_Context* ctx, bt_Thread* thread)
{
	if (!BT_IS_OBJECT(bt_arg(thread, 0))) return;

	bt_Object* arg = BT_AS_OBJECT(bt_arg(thread, 0));
	bt_return(thread, BT_VALUE_NUMBER(bt_remove_ref(ctx, arg)));
}

static void btstd_push_root(bt_Context* ctx, bt_Thread* thread)
{
	if (!BT_IS_OBJECT(bt_arg(thread, 0))) bt_runtime_error(thread, "Can't push non-reference object as root!", NULL);

	bt_Object* arg = BT_AS_OBJECT(bt_arg(thread, 0));
	bt_push_root(ctx, arg);
}

static void btstd_pop_root(bt_Context* ctx, bt_Thread* thread)
{
	bt_pop_root(ctx);
}

static void btstd_register_type(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value name = bt_arg(thread, 0);
	bt_Type* type = (bt_Type*)BT_AS_OBJECT(bt_arg(thread, 1));

	bt_register_type(ctx, name, type);
}

static void btstd_find_type(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value name = bt_arg(thread, 0);

	bt_Type* type = bt_find_type(ctx, name);

	bt_return(thread, type ? BT_VALUE_OBJECT(type) : BT_VALUE_NULL);
}

static void btstd_get_enum_name(bt_Context* ctx, bt_Thread* thread)
{
	bt_Type* enum_ = (bt_Type*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_Value value = bt_arg(thread, 1);

	if (enum_->category != BT_TYPE_CATEGORY_ENUM) {
		bt_runtime_error(thread, "meta.get_enum_name: Type provided was not enum!", NULL);
	}

	bt_Value result = bt_enum_contains(ctx, enum_, value);

	if (result == BT_VALUE_NULL) {
		bt_runtime_error(thread, "meta.get_enum_name: enum did not contain provided option", NULL);
	}

	bt_return(thread, result);
}

static void btstd_add_module_path(bt_Context* ctx, bt_Thread* thread)
{
	bt_String* pathspec = (bt_String*)BT_AS_OBJECT(bt_arg(thread, 0));

	bt_append_module_path(ctx, BT_STRING_STR(pathspec));
}

static void btstd_get_union_size(bt_Context* ctx, bt_Thread* thread)
{
	bt_Type* u = bt_type_dealias((bt_Type*)BT_AS_OBJECT(bt_arg(thread, 0)));
	if (u->category != BT_TYPE_CATEGORY_UNION) bt_runtime_error(thread, "Non-union type passed to function!", NULL);

	bt_return(thread, BT_VALUE_NUMBER(u->as.selector.types.length));
}

static void btstd_get_union_entry(bt_Context* ctx, bt_Thread* thread)
{
	bt_Type* u = bt_type_dealias((bt_Type*)BT_AS_OBJECT(bt_arg(thread, 0)));
	bt_number idx = BT_AS_NUMBER(bt_arg(thread, 1));
	if (u->category != BT_TYPE_CATEGORY_UNION) bt_runtime_error(thread, "Non-union type passed to function!", NULL);
	if (idx < 0 || idx >= u->as.selector.types.length) bt_runtime_error(thread, "Union index out of bounds!", NULL);

	bt_return(thread, BT_VALUE_OBJECT(u->as.selector.types.elements[(uint64_t)idx]));
}

static bt_Type* btstd_dump_type(bt_Context* ctx, bt_Type** args, uint8_t argc)
{
	if (argc != 1) return NULL;
	bt_Type* fn = args[0];
	if (fn->category != BT_TYPE_CATEGORY_SIGNATURE) return NULL;

	bt_Type* sig = bt_make_signature(ctx, ctx->types.string, args, 1);

	return sig;
}

static void btstd_dump(bt_Context* ctx, bt_Thread* thread)
{
	bt_Callable* arg = (bt_Callable*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_return(thread, BT_VALUE_OBJECT(bt_debug_dump_fn(ctx, arg)));
}

void boltstd_open_meta(bt_Context* context)
{
	bt_Module* module = bt_make_user_module(context);

	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "stack_size"),     bt_make_number(BT_STACK_SIZE));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "callstack_size"), bt_make_number(BT_CALLSTACK_SIZE));
	bt_module_export(context, module, context->types.string, BT_VALUE_CSTRING(context, "version"),        bt_make_object((bt_Object*)bt_make_string(context, BOLT_VERSION)));

	bt_Type* findtype_ret = bt_make_nullable(context, context->types.type);
	
	bt_Type* regtype_args[]         = { context->types.string, context->types.type   };
	bt_Type* getenumname_args[]     = { context->types.type,   context->types.any    };
	bt_Type* get_union_entry_args[] = { context->types.type,   context->types.number };

	bt_module_export_native(context, module, "gc",               btstd_gc,               context->types.number, NULL,                   0);
	bt_module_export_native(context, module, "grey",             btstd_grey,             NULL,                  &context->types.any,    1);
	bt_module_export_native(context, module, "push_root",        btstd_push_root,        NULL,                  &context->types.any,    1);
	bt_module_export_native(context, module, "pop_root",         btstd_pop_root,         NULL,                  NULL,                   0);
	bt_module_export_native(context, module, "add_reference",    btstd_add_reference,    context->types.number, &context->types.any,    1);
	bt_module_export_native(context, module, "remove_reference", btstd_remove_reference, context->types.number, &context->types.any,    1);
	bt_module_export_native(context, module, "mem_size",         btstd_memsize,          context->types.number, NULL,                   0);
	bt_module_export_native(context, module, "next_cycle",       btstd_nextcycle,        context->types.number, NULL,                   0);
	bt_module_export_native(context, module, "register_type",    btstd_register_type,    NULL,                  regtype_args,           2);
	bt_module_export_native(context, module, "find_type",        btstd_find_type,        findtype_ret,          &context->types.string, 1);
	bt_module_export_native(context, module, "get_enum_name",    btstd_get_enum_name,    context->types.string, getenumname_args,       2);
	bt_module_export_native(context, module, "add_module_path",  btstd_add_module_path,  NULL,                  &context->types.string, 1);
	bt_module_export_native(context, module, "get_union_size",   btstd_get_union_size,   context->types.number, &context->types.type,   1);
	bt_module_export_native(context, module, "get_union_entry",  btstd_get_union_entry,  context->types.type,   get_union_entry_args,   2);

	bt_Type* dump_sig = bt_make_poly_signature(context, "dump(fn): string", btstd_dump_type);
	bt_module_export(context, module, dump_sig, BT_VALUE_CSTRING(context, "dump"), BT_VALUE_OBJECT(
		bt_make_native(context, dump_sig, btstd_dump)));

	bt_register_module(context, BT_VALUE_CSTRING(context, "meta"), module);
}