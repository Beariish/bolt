#include "boltstd_meta.h"

#include "../bt_embedding.h"
#include "../bt_type.h"

static void btstd_argc(bt_Context* ctx, bt_Thread* thread)
{
	thread->depth--;
	uint8_t argc = bt_argc(thread);
	thread->depth++;
	bt_return(thread, BT_VALUE_NUMBER(argc));
}

static void btstd_arg(bt_Context* ctx, bt_Thread* thread)
{
	bt_number arg = bt_get_number(bt_arg(thread, 0));
	uint8_t arg_idx = (uint8_t)arg;

	thread->depth--;
	bt_Value value = bt_arg(thread, arg_idx);
	thread->depth++;
	bt_return(thread, value);
}

static void btstd_gc(bt_Context* ctx, bt_Thread* thread)
{
	bt_collect(&ctx->gc, 0);
}

static void btstd_memsize(bt_Context* ctx, bt_Thread* thread)
{
	bt_return(thread, bt_make_number(ctx->gc.byets_allocated));
}

static void btstd_nextcycle(bt_Context* ctx, bt_Thread* thread)
{
	bt_return(thread, bt_make_number(ctx->gc.next_cycle));
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

static void btstd_register_prelude(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value name = bt_arg(thread, 0);
	bt_Type* type = BT_AS_OBJECT(bt_arg(thread, 1));
	bt_Value value = bt_arg(thread, 2);

	bt_register_prelude(ctx, name, type, value);
}

static void btstd_register_type(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value name = bt_arg(thread, 0);
	bt_Type* type = BT_AS_OBJECT(bt_arg(thread, 1));

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
	bt_Type* enum_ = BT_AS_OBJECT(bt_arg(thread, 0));
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
	bt_String* pathspec = BT_AS_OBJECT(bt_arg(thread, 0));

	bt_append_module_path(ctx, pathspec->str);
}

void boltstd_open_meta(bt_Context* context)
{
	bt_Module* module = bt_make_user_module(context);

	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "stack_size"), bt_make_number(BT_STACK_SIZE));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "callstack_size"), bt_make_number(BT_CALLSTACK_SIZE));

	bt_Type* arg_args[] = { context->types.number };
	bt_Type* arg_sig = bt_make_signature(context, context->types.any, arg_args, 1);
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

	//bt_module_export(context, module, arg_sig, BT_VALUE_CSTRING(context, "arg"), BT_VALUE_OBJECT(
	//	bt_make_native(context, arg_sig, btstd_arg)));

	bt_module_export(context, module, info_sig, BT_VALUE_CSTRING(context, "argc"), BT_VALUE_OBJECT(
		bt_make_native(context, info_sig, btstd_argc)));

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

	bt_module_export(context, module, prelude_sig, BT_VALUE_CSTRING(context, "register_prelude"), BT_VALUE_OBJECT(
		bt_make_native(context, prelude_sig, btstd_register_prelude)));

	bt_module_export(context, module, regtype_sig, BT_VALUE_CSTRING(context, "register_type"), BT_VALUE_OBJECT(
		bt_make_native(context, regtype_sig, btstd_register_type)));

	bt_module_export(context, module, findtype_sig, BT_VALUE_CSTRING(context, "find_type"), BT_VALUE_OBJECT(
		bt_make_native(context, findtype_sig, btstd_find_type)));

	bt_module_export(context, module, getenumname_sig, BT_VALUE_CSTRING(context, "get_enum_name"), BT_VALUE_OBJECT(
		bt_make_native(context, getenumname_sig, btstd_get_enum_name)));

	bt_module_export(context, module, findtype_sig, BT_VALUE_CSTRING(context, "add_mdoule_path"), BT_VALUE_OBJECT(
		bt_make_native(context, findtype_sig, btstd_add_module_path)));

	bt_register_module(context, BT_VALUE_CSTRING(context, "meta"), module);
}