#include "boltstd_meta.h"

#include "../bt_embedding.h"
#include "../bt_type.h"
#include "../bt_debug.h"

static const char* annotation_type_name = "Annotation";
static const char* annotation_name_key_name = "name";
static const char* annotation_args_key_name = "args";

static void btstd_gc(bt_Context* ctx, bt_Thread* thread)
{
	uint32_t n_collected = bt_collect(&ctx->gc, 0);
	bt_return(thread, BT_VALUE_NUMBER(n_collected));
}

static void btstd_memsize(bt_Context* ctx, bt_Thread* thread)
{
	bt_return(thread, bt_make_number((bt_number)ctx->gc.bytes_allocated));
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

	bt_Type* sig = bt_make_signature_type(ctx, ctx->types.string, args, 1);

	return sig;
}

static void btstd_dump(bt_Context* ctx, bt_Thread* thread)
{
	bt_Callable* arg = (bt_Callable*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_return(thread, BT_VALUE_OBJECT(bt_debug_dump_fn(ctx, arg)));
}

static void populate_annotation_array(bt_Context* ctx, bt_Type* anno_type, bt_Annotation* anno, bt_Array* array) {
	while (anno) {
		bt_Table* bt_anno = bt_make_table_from_proto(ctx, anno_type);
		bt_table_set(ctx, bt_anno, BT_VALUE_CSTRING(ctx, annotation_name_key_name), BT_VALUE_OBJECT(anno->name));
		bt_Array* args = anno->args ? anno->args : bt_make_array(ctx, 0);
		bt_table_set(ctx, bt_anno, BT_VALUE_CSTRING(ctx, annotation_args_key_name), BT_VALUE_OBJECT(args));
		bt_array_push(ctx, array, BT_VALUE_OBJECT(bt_anno));

		anno = anno->next;
	}
}

static void btstd_get_annotations(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value arg = bt_arg(thread, 0);
	bt_Array* ret = bt_make_array(ctx, 1);
	bt_return(thread, BT_VALUE_OBJECT(ret));

	if (BT_IS_OBJECT(arg)) {
		bt_Module* module = bt_get_module(thread);
		bt_Type* annotation_type = (bt_Type*)bt_object(bt_module_get_storage(module, BT_VALUE_CSTRING(ctx, annotation_type_name)));

		bt_Object* as_obj = (bt_Object*)BT_AS_OBJECT(arg);
		bt_Annotation* anno = NULL;

		if (BT_OBJECT_GET_TYPE(as_obj) == BT_OBJECT_TYPE_FN) anno = ((bt_Fn*)as_obj)->signature->annotations;
		if (BT_OBJECT_GET_TYPE(as_obj) == BT_OBJECT_TYPE_CLOSURE) anno = ((bt_Closure*)as_obj)->fn->signature->annotations;
		if (BT_OBJECT_GET_TYPE(as_obj) == BT_OBJECT_TYPE_TYPE) anno = ((bt_Type*)as_obj)->annotations;

		populate_annotation_array(ctx, annotation_type, anno, ret);
	}
}

static void btstd_get_field_annotations(bt_Context* ctx, bt_Thread* thread)
{
	bt_Type* type = (bt_Type*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_Value key = bt_arg(thread, 1);
	bt_Array* ret = bt_make_array(ctx, 1);
	bt_return(thread, BT_VALUE_OBJECT(ret));

	if (type->category == BT_TYPE_CATEGORY_TABLESHAPE) {
		bt_Module* module = bt_get_module(thread);
		bt_Type* annotation_type = (bt_Type*)bt_object(bt_module_get_storage(module, BT_VALUE_CSTRING(ctx, annotation_type_name)));
		
		bt_Annotation* anno = bt_tableshape_get_field_annotations(type, key);
		populate_annotation_array(ctx, annotation_type, anno, ret);
	}
}

void boltstd_open_meta(bt_Context* context)
{
	bt_Module* module = bt_make_module(context);
	bt_Type* any = bt_type_any(context);
	
	bt_Type* number = bt_type_number(context);
	bt_Type* string = bt_type_string(context);
	bt_Type* type = bt_type_type(context);

	bt_Type* annotation_type = bt_make_tableshape_type(context, annotation_type_name, BT_TRUE);
	bt_tableshape_add_layout(context, annotation_type, bt_type_string(context), BT_VALUE_CSTRING(context, annotation_name_key_name), bt_type_string(context));
	bt_tableshape_add_layout(context, annotation_type, bt_type_string(context), BT_VALUE_CSTRING(context, annotation_args_key_name), bt_make_array_type(context, any));
	bt_module_set_storage(module, BT_VALUE_CSTRING(context, annotation_type_name), bt_value(annotation_type));
	
	bt_module_export(context, module, number, BT_VALUE_CSTRING(context, "stack_size"),     bt_make_number(BT_STACK_SIZE));
	bt_module_export(context, module, number, BT_VALUE_CSTRING(context, "callstack_size"), bt_make_number(BT_CALLSTACK_SIZE));
	bt_module_export(context, module, string, BT_VALUE_CSTRING(context, "version"),        bt_value((bt_Object*)bt_make_string(context, BOLT_VERSION)));
	bt_module_export(context, module, type,   BT_VALUE_CSTRING(context, "Annotation"),     bt_value((bt_Object*)annotation_type));
	
	bt_Type* findtype_ret = bt_type_make_nullable(context, type);
	bt_Type* annotation_arr = bt_make_array_type(context, annotation_type);
	
	bt_Type* regtype_args[]         = { string, type };
	bt_Type* getenumname_args[]     = { type,   any };
	bt_Type* get_union_entry_args[] = { type,   number };
	bt_Type* field_anno_args[]      = { type,   any };

	bt_module_export_native(context, module, "gc",                btstd_gc,                    number,         NULL,                 0);
	bt_module_export_native(context, module, "grey",              btstd_grey,                  NULL,           &any,                 1);
	bt_module_export_native(context, module, "push_root",         btstd_push_root,             NULL,           &any,                 1);
	bt_module_export_native(context, module, "pop_root",          btstd_pop_root,              NULL,           NULL,                 0);
	bt_module_export_native(context, module, "add_reference",     btstd_add_reference,         number,         &any,                 1);
	bt_module_export_native(context, module, "remove_reference",  btstd_remove_reference,      number,         &any,                 1);
	bt_module_export_native(context, module, "mem_size",          btstd_memsize,               number,         NULL,                 0);
	bt_module_export_native(context, module, "next_cycle",        btstd_nextcycle,             number,         NULL,                 0);
	bt_module_export_native(context, module, "register_type",     btstd_register_type,         NULL,           regtype_args,         2);
	bt_module_export_native(context, module, "find_type",         btstd_find_type,             findtype_ret,   &string,              1);
	bt_module_export_native(context, module, "get_enum_name",     btstd_get_enum_name,         string,         getenumname_args,     2);
	bt_module_export_native(context, module, "add_module_path",   btstd_add_module_path,       NULL,           &string,              1);
	bt_module_export_native(context, module, "get_union_size",    btstd_get_union_size,        number,         &type,                1);
	bt_module_export_native(context, module, "get_union_entry",   btstd_get_union_entry,       type,           get_union_entry_args, 2);
	bt_module_export_native(context, module, "annotations",       btstd_get_annotations,       annotation_arr, &any,                 1);
	bt_module_export_native(context, module, "field_annotations", btstd_get_field_annotations, annotation_arr, field_anno_args,      2);
	
	bt_Type* dump_sig = bt_make_poly_signature_type(context, "dump(fn): string", btstd_dump_type);
	bt_module_export(context, module, dump_sig, BT_VALUE_CSTRING(context, "dump"), BT_VALUE_OBJECT(
		bt_make_native(context, module, dump_sig, btstd_dump)));

	bt_register_module(context, BT_VALUE_CSTRING(context, "meta"), module);
}