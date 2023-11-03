#include "boltstd_arrays.h"

#include "../bt_embedding.h"

static void bt_arr_length(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value arg = bt_arg(thread, 0);
	bt_Array* as_arr = (bt_Array*)BT_AS_OBJECT(arg);
	bt_return(thread, BT_VALUE_NUMBER(as_arr->items.length));
}

static void bt_arr_pop(bt_Context* ctx, bt_Thread* thread)
{
	bt_Array* as_arr = (bt_Array*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_return(thread, bt_array_pop(as_arr));
}

static bt_Type* bt_arr_pop_type(bt_Context* ctx, bt_Type** args, uint8_t argc)
{
	if (argc != 1) return NULL;
	bt_Type* arg = args[0];
	if (arg->category != BT_TYPE_CATEGORY_ARRAY) return NULL;

	bt_Type* sig = bt_make_method(ctx, arg->as.array.inner, args, 1);

	return sig;
}

static void bt_arr_push(bt_Context* ctx, bt_Thread* thread)
{
	bt_Array* as_arr = (bt_Array*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_Value to_push = bt_arg(thread, 1);
	bt_array_push(ctx, as_arr, to_push);
}

static bt_Type* bt_arr_push_type(bt_Context* ctx, bt_Type** args, uint8_t argc)
{
	if (argc != 2) return NULL;
	bt_Type* array = args[0];
	if (array->category != BT_TYPE_CATEGORY_ARRAY) return NULL;

	bt_Type* new_args[] = { array, array->as.array.inner };
	bt_Type* sig = bt_make_method(ctx, NULL, new_args, 2);

	return sig;
}

static bt_Value bt_arr_each_iter_fn;

static void bt_arr_each(bt_Context* ctx, bt_Thread* thread)
{
	bt_push(thread, bt_arr_each_iter_fn);
	bt_push(thread, bt_arg(thread, 0));
	bt_push(thread, BT_VALUE_NUMBER(0));

	bt_return(thread, bt_make_closure(thread, 2));
}

static void bt_arr_each_iter(bt_Context* ctx, bt_Thread* thread)
{
	bt_Array* arr = (bt_Array*)BT_AS_OBJECT(bt_getup(thread, 0));
	bt_number idx = BT_AS_NUMBER(bt_getup(thread, 1));

	if (idx >= arr->items.length) {
		bt_return(thread, BT_VALUE_NULL);
	}
	else {
		bt_return(thread, bt_array_get(ctx, arr, (uint64_t)idx++));
		bt_setup(thread, 1, BT_VALUE_NUMBER(idx));
	}
}

static bt_Type* bt_arr_each_type(bt_Context* ctx, bt_Type** args, uint8_t argc)
{
	if (argc != 1) return NULL;
	bt_Type* arg = bt_type_dealias(args[0]);

	if (arg->category != BT_TYPE_CATEGORY_ARRAY) return NULL;

	bt_Type* iter_sig = bt_make_signature(ctx, bt_make_nullable(ctx, arg->as.array.inner), NULL, 0);

	bt_Type* sig = bt_make_method(ctx, iter_sig, &arg, 1);

	return sig;
}

static bt_Type* bt_arr_reverse_type(bt_Context* ctx, bt_Type** args, uint8_t argc)
{
	if (argc != 1) return NULL;
	bt_Type* arg = bt_type_dealias(args[0]);

	if (arg->category != BT_TYPE_CATEGORY_ARRAY) return NULL;

	return bt_make_method(ctx, NULL, &arg, 1);
}

static bt_Type* bt_arr_clone_type(bt_Context* ctx, bt_Type** args, uint8_t argc)
{
	if (argc != 1) return NULL;
	bt_Type* arg = bt_type_dealias(args[0]);

	if (arg->category != BT_TYPE_CATEGORY_ARRAY) return NULL;

	return bt_make_method(ctx, arg, &arg, 1);
}

static void bt_arr_reverse(bt_Context* ctx, bt_Thread* thread)
{
	bt_Array* arr = (bt_Array*)bt_get_object(bt_arg(thread, 0));

	uint32_t i = arr->items.length - 1;
	uint32_t j = 0;
	while (i > j)
	{
		bt_Value temp = arr->items.elements[i];
		arr->items.elements[i] = arr->items.elements[j];
		arr->items.elements[j] = temp;
		i--;
		j++;
	}
}

static void bt_arr_clone(bt_Context* ctx, bt_Thread* thread)
{
	bt_Array* arr = (bt_Array*)bt_get_object(bt_arg(thread, 0));

	bt_Array* clone = bt_make_array(ctx, arr->items.length);
	bt_buffer_append(ctx, &clone->items, &arr->items);

	bt_return(thread, BT_VALUE_OBJECT(clone));
}

static bt_Type* bt_arr_map_type(bt_Context* ctx, bt_Type** args, uint8_t argc)
{
	if (argc != 2) return NULL;
	bt_Type* arg = bt_type_dealias(args[0]);

	if (arg->category != BT_TYPE_CATEGORY_ARRAY) return NULL;

	bt_Type* applicator = bt_type_dealias(args[1]);

	if (applicator->category != BT_TYPE_CATEGORY_SIGNATURE) return NULL;

	if (!applicator->as.fn.return_type) return NULL;
	if (applicator->as.fn.args.length != 1) return NULL;
	if (!applicator->as.fn.args.elements[0]->satisfier(
		applicator->as.fn.args.elements[0], arg->as.array.inner)) return NULL;

	bt_Type* return_type = bt_make_array_type(ctx, applicator->as.fn.return_type);

	return bt_make_method(ctx, return_type, args, 2);
}

static void bt_arr_map(bt_Context* ctx, bt_Thread* thread)
{
	bt_Array* arg = (bt_Array*)bt_get_object(bt_arg(thread, 0));
	bt_Value applicator = bt_arg(thread, 1);

	bt_Array* result = bt_make_array(ctx, arg->items.length);

	for (uint32_t i = 0; i < arg->items.length; ++i) {
		bt_push(thread, applicator);
		bt_push(thread, arg->items.elements[i]);
		bt_call(thread, 1);

		bt_Value mapped = bt_pop(thread);
		bt_array_push(ctx, result, mapped);
	}

	bt_return(thread, BT_VALUE_OBJECT(result));
}

static bt_Type* bt_arr_filter_type(bt_Context* ctx, bt_Type** args, uint8_t argc)
{
	if (argc != 2) return NULL;
	bt_Type* arg = bt_type_dealias(args[0]);

	if (arg->category != BT_TYPE_CATEGORY_ARRAY) return NULL;

	bt_Type* applicator = bt_type_dealias(args[1]);

	if (applicator->category != BT_TYPE_CATEGORY_SIGNATURE) return NULL;

	if (applicator->as.fn.return_type != ctx->types.boolean) return NULL;
	if (applicator->as.fn.args.length != 1) return NULL;
	if (!applicator->as.fn.args.elements[0]->satisfier(
		applicator->as.fn.args.elements[0], arg->as.array.inner)) return NULL;

	bt_Type* return_type = bt_make_array_type(ctx, arg->as.array.inner);

	return bt_make_method(ctx, return_type, args, 2);
}

static void bt_arr_filter(bt_Context* ctx, bt_Thread* thread)
{
	bt_Array* arg = (bt_Array*)bt_get_object(bt_arg(thread, 0));
	bt_Value filter = bt_arg(thread, 1);

	bt_Array* result = bt_make_array(ctx, arg->items.length / 2);

	for (uint32_t i = 0; i < arg->items.length; ++i) {
		bt_push(thread, filter);
		bt_push(thread, arg->items.elements[i]);
		bt_call(thread, 1);

		bt_Value filtered = bt_pop(thread);
		if (filtered == BT_VALUE_TRUE) {
			bt_array_push(ctx, result, arg->items.elements[i]);
		}
	}

	bt_return(thread, BT_VALUE_OBJECT(result));
}

void boltstd_open_arrays(bt_Context* context)
{
	bt_Module* module = bt_make_user_module(context);
	bt_Type* array = context->types.array;

	bt_Type* length_sig = bt_make_method(context, context->types.number, &context->types.array, 1);
	bt_NativeFn* fn_ref = bt_make_native(context, length_sig, bt_arr_length);
	bt_type_add_field(context, array, length_sig, BT_VALUE_CSTRING(context, "length"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, length_sig, BT_VALUE_CSTRING(context, "length"), BT_VALUE_OBJECT(fn_ref));

	bt_Type* arr_pop_sig = bt_make_poly_method(context, "pop([T]): T", bt_arr_pop_type);
	fn_ref = bt_make_native(context, arr_pop_sig, bt_arr_pop);
	bt_type_add_field(context, array, arr_pop_sig, BT_VALUE_CSTRING(context, "pop"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, arr_pop_sig, BT_VALUE_CSTRING(context, "pop"), BT_VALUE_OBJECT(fn_ref));

	bt_Type* arr_push_sig = bt_make_poly_method(context, "push([T], T)", bt_arr_push_type);
	fn_ref = bt_make_native(context, arr_push_sig, bt_arr_push);
	bt_type_add_field(context, array, arr_push_sig, BT_VALUE_CSTRING(context, "push"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, arr_push_sig, BT_VALUE_CSTRING(context, "push"), BT_VALUE_OBJECT(fn_ref));

	bt_arr_each_iter_fn = BT_VALUE_OBJECT(bt_make_native(context, NULL, bt_arr_each_iter));
	bt_Type* arr_each_sig = bt_make_poly_method(context, "each([T]): fn: T?", bt_arr_each_type);
	fn_ref = bt_make_native(context, arr_each_sig, bt_arr_each);
	bt_type_add_field(context, array, arr_each_sig, BT_VALUE_CSTRING(context, "each"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, arr_each_sig, BT_VALUE_CSTRING(context, "each"), BT_VALUE_OBJECT(fn_ref));
	bt_type_add_field(context, array, arr_each_sig, BT_VALUE_CSTRING(context, "$_each_iter"), bt_arr_each_iter_fn);

	bt_Type* arr_clone_sig = bt_make_poly_method(context, "clone([T]): [T]", bt_arr_clone_type);
	fn_ref = bt_make_native(context, arr_clone_sig, bt_arr_clone);
	bt_type_add_field(context, array, arr_clone_sig, BT_VALUE_CSTRING(context, "clone"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, arr_clone_sig, BT_VALUE_CSTRING(context, "clone"), BT_VALUE_OBJECT(fn_ref));

	bt_Type* arr_reverse_sig = bt_make_poly_method(context, "reverse([T]): [T]", bt_arr_reverse_type);
	fn_ref = bt_make_native(context, arr_reverse_sig, bt_arr_reverse);
	bt_type_add_field(context, array, arr_reverse_sig, BT_VALUE_CSTRING(context, "reverse"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, arr_reverse_sig, BT_VALUE_CSTRING(context, "reverse"), BT_VALUE_OBJECT(fn_ref));

	bt_Type* arr_map_sig = bt_make_poly_method(context, "map([T], fn(T): R): [R]", bt_arr_map_type);
	fn_ref = bt_make_native(context, arr_map_sig, bt_arr_map);
	bt_type_add_field(context, array, arr_map_sig, BT_VALUE_CSTRING(context, "map"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, arr_map_sig, BT_VALUE_CSTRING(context, "map"), BT_VALUE_OBJECT(fn_ref));

	bt_Type* arr_filter_sig = bt_make_poly_method(context, "filter([T], fn(T): bool): [T]", bt_arr_filter_type);
	fn_ref = bt_make_native(context, arr_filter_sig, bt_arr_filter);
	bt_type_add_field(context, array, arr_filter_sig, BT_VALUE_CSTRING(context, "filter"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, arr_filter_sig, BT_VALUE_CSTRING(context, "filter"), BT_VALUE_OBJECT(fn_ref));

	bt_register_module(context, BT_VALUE_CSTRING(context, "arrays"), module);
}