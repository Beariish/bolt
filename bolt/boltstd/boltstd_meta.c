#include "boltstd_meta.h"

#include "../bt_embedding.h"
#include "../bt_type.h"

static void btstd_gc(bt_Context* ctx, bt_Thread* thread)
{
	bt_collect(&ctx->gc, 0);
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

void boltstd_open_meta(bt_Context* context)
{
	bt_Module* module = bt_make_user_module(context);

	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "stack_size"), bt_make_number(BT_STACK_SIZE));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "callstack_size"), bt_make_number(BT_CALLSTACK_SIZE));

	bt_Type* info_sig = bt_make_signature(context, context->types.number, NULL, 0);

	bt_Type* grey_args[] = { context->types.any };

	bt_Type* gc_sig = bt_make_signature(context, NULL, NULL, 0);
	bt_Type* grey_sig = bt_make_signature(context, NULL, grey_args, 1);

	bt_Type* prelude_args[] = { context->types.string, context->types.type, context->types.any };
	bt_Type* prelude_sig = bt_make_signature(context, NULL, prelude_args, 3);

	bt_Type* regtype_args[] = { context->types.string, context->types.type };
	bt_Type* regtype_sig = bt_make_signature(context, NULL, regtype_args, 2);

	bt_Type* findtype_args[] = { context->types.string };
	bt_Type* findtype_sig = bt_make_signature(context, bt_make_nullable(context, context->types.type), findtype_args, 1);

	bt_Type* getenumname_args[] = { context->types.type, context->types.any };
	bt_Type* getenumname_sig = bt_make_signature(context, context->types.string, getenumname_args, 2);

	bt_Type* get_union_size_args[] = { context->types.type };
	bt_Type* get_union_size_sig = bt_make_signature(context, context->types.number, get_union_size_args, 1);

	bt_Type* get_union_entry_args[] = { context->types.type, context->types.number };
	bt_Type* get_union_entry_sig = bt_make_signature(context, context->types.type, get_union_entry_args, 2);

	bt_module_export(context, module, gc_sig, BT_VALUE_CSTRING(context, "gc"), BT_VALUE_OBJECT(
		bt_make_native(context, gc_sig, btstd_gc)));

	bt_module_export(context, module, grey_sig, BT_VALUE_CSTRING(context, "grey"), BT_VALUE_OBJECT(
		bt_make_native(context, grey_sig, btstd_grey)));

	bt_module_export(context, module, grey_sig, BT_VALUE_CSTRING(context, "push_root"), BT_VALUE_OBJECT(
		bt_make_native(context, grey_sig, btstd_push_root)));

	bt_module_export(context, module, gc_sig, BT_VALUE_CSTRING(context, "pop_root"), BT_VALUE_OBJECT(
		bt_make_native(context, gc_sig, btstd_pop_root)));

	bt_module_export(context, module, info_sig, BT_VALUE_CSTRING(context, "mem_size"), BT_VALUE_OBJECT(
		bt_make_native(context, info_sig, btstd_memsize)));

	bt_module_export(context, module, info_sig, BT_VALUE_CSTRING(context, "next_cycle"), BT_VALUE_OBJECT(
		bt_make_native(context, info_sig, btstd_nextcycle)));

	bt_module_export(context, module, regtype_sig, BT_VALUE_CSTRING(context, "register_type"), BT_VALUE_OBJECT(
		bt_make_native(context, regtype_sig, btstd_register_type)));

	bt_module_export(context, module, findtype_sig, BT_VALUE_CSTRING(context, "find_type"), BT_VALUE_OBJECT(
		bt_make_native(context, findtype_sig, btstd_find_type)));

	bt_module_export(context, module, getenumname_sig, BT_VALUE_CSTRING(context, "get_enum_name"), BT_VALUE_OBJECT(
		bt_make_native(context, getenumname_sig, btstd_get_enum_name)));

	bt_module_export(context, module, findtype_sig, BT_VALUE_CSTRING(context, "add_module_path"), BT_VALUE_OBJECT(
		bt_make_native(context, findtype_sig, btstd_add_module_path)));

	bt_module_export(context, module, get_union_size_sig, BT_VALUE_CSTRING(context, "get_union_size"), BT_VALUE_OBJECT(
		bt_make_native(context, get_union_size_sig, btstd_get_union_size)));

	bt_module_export(context, module, get_union_entry_sig, BT_VALUE_CSTRING(context, "get_union_entry"), BT_VALUE_OBJECT(
		bt_make_native(context, get_union_entry_sig, btstd_get_union_entry)));

	bt_register_module(context, BT_VALUE_CSTRING(context, "meta"), module);
}