#include "bt_type.h"
#include "bt_context.h"

#include <memory.h>
#include <string.h>

bt_bool bt_type_satisfier_any(bt_Type* left, bt_Type* right)
{
	return BT_TRUE;
}

bt_bool bt_type_satisfier_null(bt_Type* left, bt_Type* right)
{
	return bt_type_satisfier_same(left, right) || left->is_optional;
}

bt_bool bt_type_satisfier_same(bt_Type* left, bt_Type* right)
{
	return left == right;
}

bt_bool bt_type_satisfier_signature(bt_Type* left, bt_Type* right)
{
	if (left->category != BT_TYPE_CATEGORY_SIGNATURE || right->category != BT_TYPE_CATEGORY_SIGNATURE)
		return BT_FALSE;

	if (left->as.fn.is_vararg && !right->as.fn.is_vararg) return BT_FALSE;

	if (left->as.fn.is_vararg) {
		if (!right->as.fn.varargs_type->satisfier(right->as.fn.varargs_type, left->as.fn.varargs_type))
			return BT_FALSE;
	}

	if (left->as.fn.args.length != right->as.fn.args.length) {
		if (left->as.fn.args.length < right->as.fn.args.length) return BT_FALSE;
		if (!right->as.fn.is_vararg) return BT_FALSE;
	}

	if (left->as.fn.return_type == 0 && right->as.fn.return_type) return BT_FALSE;
	if (left->as.fn.return_type && right->as.fn.return_type == 0) return BT_FALSE;

	if (left->as.fn.return_type) {
		if (!left->as.fn.return_type->satisfier(left->as.fn.return_type, right->as.fn.return_type))
			return BT_FALSE;
	}

	uint32_t n_typed_args = left->as.fn.args.length < right->as.fn.args.length ?
		left->as.fn.args.length : right->as.fn.args.length;

	for (uint32_t i = 0; i < n_typed_args; ++i) {
		bt_Type* arg_left = *(bt_Type**)bt_buffer_at(&left->as.fn.args, i);
		bt_Type* arg_right = *(bt_Type**)bt_buffer_at(&right->as.fn.args, i);
		
		if (!arg_right->satisfier(arg_right, arg_left))
			return BT_FALSE;
	}

	uint32_t n_unnamed_args = left->as.fn.args.length - n_typed_args;
	for (uint32_t i = 0; i < n_unnamed_args; ++i) {
		bt_Type* arg_left = *(bt_Type**)bt_buffer_at(&left->as.fn.args, n_typed_args + i);
		bt_Type* arg_right = right->as.fn.varargs_type;
	
		if (!arg_right->satisfier(arg_right, arg_left))
			return BT_FALSE;
	}

	return BT_TRUE;
}

bt_bool bt_type_satisfier_array(bt_Type* left, bt_Type* right)
{
	return bt_type_satisfier_same(left, right) || (
		(left->category == BT_TYPE_CATEGORY_ARRAY && left->category == right->category) &&
		left->as.array.inner->satisfier(
			left->as.array.inner,
			right->as.array.inner));
}

bt_bool bt_type_satisfier_table(bt_Type* left, bt_Type* right)
{
	if (left->category != BT_TYPE_CATEGORY_TABLESHAPE || right->category != BT_TYPE_CATEGORY_TABLESHAPE) return BT_FALSE;

	if (right->as.table_shape.parent) {
		if (bt_type_satisfier_table(left, right->as.table_shape.parent)) {
			return BT_TRUE;
		}
	}

	if (left->as.table_shape.sealed && right->as.table_shape.layout->pairs.length != left->as.table_shape.layout->pairs.length) return BT_FALSE;

	if (left->as.table_shape.values &&
		left->as.table_shape.values != right->as.table_shape.values) {
		return BT_FALSE;
	}

	bt_Buffer* lpairs = &left->as.table_shape.layout->pairs;
	bt_Buffer* rpairs = &right->as.table_shape.layout->pairs;
	
	for (uint32_t i = 0; i < lpairs->length; ++i) {
		bt_TablePair* lentry = bt_buffer_at(lpairs, i);

		bt_bool found = BT_FALSE;
		for (uint32_t j = 0; j < rpairs->length; ++j) {
			bt_TablePair* rentry = bt_buffer_at(rpairs, j);

			bt_Type* ltype = BT_AS_OBJECT(lentry->value);
			bt_Type* rtype = BT_AS_OBJECT(rentry->value);

			if (bt_value_is_equal(lentry->key, rentry->key) &&
				ltype->satisfier(ltype, rtype)) {
				found = BT_TRUE;
				break;
			}
		}

		if (found == BT_FALSE) return BT_FALSE;
	}

	return BT_TRUE;
}

bt_bool bt_type_satisfier_union(bt_Type* left, bt_Type* right)
{
	if (left->category != BT_TYPE_CATEGORY_UNION) return BT_FALSE;

	bt_Buffer* types = &left->as.selector.types;

	if (right->category == BT_TYPE_CATEGORY_UNION) {
		bt_Buffer* rtypes = &right->as.selector.types;
		for (uint32_t i = 0; i < rtypes->length; ++i) {
			bt_Type* rtype = *(bt_Type**)bt_buffer_at(rtypes, i);
			
			bt_bool found = BT_FALSE;

			for (uint32_t j = 0; j < types->length; ++j) {
				bt_Type* type = *(bt_Type**)bt_buffer_at(types, j);
				if (type->satisfier(type, rtype)) {
					found = BT_TRUE;
					break;
				}
			}

			if (!found) {
				return BT_FALSE;
			}
		}

		return BT_TRUE;
	}
	else {
		for (uint32_t i = 0; i < types->length; ++i) {
			bt_Type* type = *(bt_Type**)bt_buffer_at(types, i);
			if (type->satisfier(type, right)) {
				return BT_TRUE;
			}
		}
	}

	return BT_FALSE;
}

bt_bool type_satisifer_nullable(bt_Type* left, bt_Type* right)
{
	if (right->is_optional) {
		return left->as.nullable.base->satisfier(left->as.nullable.base, right->as.nullable.base);
	}

	return left->as.nullable.base->satisfier(left->as.nullable.base, right) ||
		(left->is_optional && right == left->ctx->types.null);
}

bt_bool type_satisfier_alias(bt_Type* left, bt_Type* right)
{
	if (right->category == BT_TYPE_CATEGORY_TYPE) {
		return left->as.type.boxed->satisfier(left->as.type.boxed, right->as.type.boxed);
	}

	return left->as.type.boxed->satisfier(left->as.type.boxed, right);
}

bt_bool type_satisfier_type(bt_Type* left, bt_Type* right)
{
	return right->category == BT_TYPE_CATEGORY_TYPE;
}

bt_Type* bt_make_type(bt_Context* context, const char* name, bt_TypeSatisfier satisfier, bt_TypeCategory category, bt_bool is_optional)
{
	bt_Type* result = BT_ALLOCATE(context, TYPE, bt_Type);
	result->ctx = context;
	
	result->name = context->alloc(strlen(name) + 1);
	strcpy(result->name, name);

	result->satisfier = satisfier;
	result->category = category;
	result->is_optional = is_optional;
	result->is_compiled = BT_FALSE;
	return result;
}

bt_Type* bt_derive_type(bt_Context* context, bt_Type* original)
{
	bt_Type* promoted = BT_ALLOCATE(context, TYPE, bt_Type);
	memcpy((char*)promoted + sizeof(bt_Object), (char*)original + sizeof(bt_Object), sizeof(bt_Type) - sizeof(bt_Object));
	
	const char* old_name = promoted->name;
	promoted->name = context->alloc(strlen(old_name) + 1);
	strcpy(promoted->name, old_name);

	return promoted;
}

bt_Type* bt_make_nullable(bt_Context* context, bt_Type* to_nullable)
{
	bt_Type* new_type = bt_derive_type(context, to_nullable);

	context->free(new_type->name);
	size_t len = strlen(to_nullable->name);
	new_type->name = context->alloc(len + 2);
	strcpy(new_type->name, to_nullable->name);

	new_type->name[len] = '?';
	new_type->name[len + 1] = 0;

	new_type->is_optional = BT_TRUE;
	new_type->as.nullable.base = to_nullable;
	new_type->satisfier = type_satisifer_nullable;
	return new_type;
}

bt_Type* bt_remove_nullable(bt_Context* context, bt_Type* to_unnull) {
	return to_unnull->as.nullable.base;
}

static void update_sig_name(bt_Context* ctx, bt_Type* fn)
{
	// TODO(bearish): For the absolute fucking love of god please rewrite this. im begging you. thanks.
	char name_buf[4096];
	char* name_buf_cur = name_buf;
	char* name_buf_base = name_buf;

	strcpy(name_buf_cur, "fn");
	name_buf_cur += strlen("fn");

	if (fn->as.fn.args.length || fn->as.fn.is_vararg) {
		strcpy(name_buf_cur, "(");
		name_buf_cur += strlen("(");
	}

	for (uint8_t i = 0; i < fn->as.fn.args.length; i++) {
		bt_Type* arg = *(bt_Type**)bt_buffer_at(&fn->as.fn.args, i);
		strcpy(name_buf_cur, arg->name);
		name_buf_cur += strlen(arg->name);
		if (i < fn->as.fn.args.length - 1) {
			strcpy(name_buf_cur, ", ");
			name_buf_cur += strlen(", ");
		}
	}

	if (fn->as.fn.is_vararg) {
		if (fn->as.fn.args.length) {
			strcpy(name_buf_cur, ", ");
			name_buf_cur += strlen(", ");
		}

		strcpy(name_buf_cur, "..");
		name_buf_cur += strlen("..");
		
		strcpy(name_buf_cur, fn->as.fn.varargs_type->name);
		name_buf_cur += strlen(fn->as.fn.varargs_type->name);
	}

	if (fn->as.fn.args.length || fn->as.fn.is_vararg) {
		strcpy(name_buf_cur, ")");
		name_buf_cur += strlen(")");
	}

	if (fn->as.fn.return_type) {
		strcpy(name_buf_cur, ": ");
		name_buf_cur += strlen(": ");
		strcpy(name_buf_cur, fn->as.fn.return_type->name);
		name_buf_cur += strlen(fn->as.fn.return_type->name);
	}

	if (fn->name) ctx->free(fn->name);

	char* new_name = ctx->alloc(name_buf_cur - name_buf_base + 1);
	memcpy(new_name, name_buf_base, name_buf_cur - name_buf_base);
	new_name[name_buf_cur - name_buf_base] = 0;

	fn->name = new_name;
}

bt_Type* bt_make_signature(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count)
{	
	bt_Type* result = bt_make_type(context, "", bt_type_satisfier_signature, BT_TYPE_CATEGORY_SIGNATURE, BT_FALSE);
	result->as.fn.return_type = ret;
	result->as.fn.args = BT_BUFFER_WITH_CAPACITY(context, bt_Type*, arg_count);
	for (uint8_t i = 0; i < arg_count; ++i) bt_buffer_push(context, &result->as.fn.args, args + i);
	result->as.fn.is_vararg = BT_FALSE;
	result->as.fn.varargs_type = NULL;
	result->as.fn.is_method = BT_FALSE;

	update_sig_name(context, result);

	return result;
}

bt_Type* bt_make_method(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count)
{
	bt_Type* result = bt_make_signature(context, ret, args, arg_count);
	result->as.fn.is_method = BT_TRUE;
	return result;
}

bt_Type* bt_make_vararg(bt_Context* context, bt_Type* original, bt_Type* varargs_type)
{
	original->as.fn.is_vararg = BT_TRUE;
	original->as.fn.varargs_type = varargs_type;

	update_sig_name(context, original);

	return original;
}

bt_Type* bt_make_alias(bt_Context* context, const char* name, bt_Type* boxed)
{
	bt_Type* result = bt_make_type(context, name, type_satisfier_alias, BT_TYPE_CATEGORY_TYPE, BT_FALSE);
	result->as.type.boxed = boxed;

	return result;
}

bt_Type* bt_make_fundamental(bt_Context* context)
{
	return bt_make_type(context, "Type", type_satisfier_type, BT_TYPE_CATEGORY_TYPE, BT_FALSE);
}

bt_Type* bt_make_userdata_type(bt_Context* context, const char* name)
{
	bt_Type* result = bt_make_type(context, name, bt_type_satisfier_same, BT_TYPE_CATEGORY_USERDATA, BT_FALSE);
	result->as.userdata.fields = bt_buffer_empty();
	result->as.userdata.functions = bt_buffer_empty();
	return result;
}

bt_Type* bt_make_tableshape(bt_Context* context, const char* name, bt_bool sealed)
{
	bt_Type* result = bt_make_type(context, name, bt_type_satisfier_table, BT_TYPE_CATEGORY_TABLESHAPE, BT_FALSE);
	result->as.table_shape.sealed = sealed;
	result->as.table_shape.layout = 0;
	result->as.table_shape.values = 0;
	result->as.table_shape.proto = 0;
	result->as.table_shape.parent = 0;
	return result;
}

void bt_tableshape_add_layout(bt_Context* context, bt_Type* tshp, bt_Value name, bt_Type* type)
{
	if (tshp->as.table_shape.layout == 0) {
		tshp->as.table_shape.layout = bt_make_table(context, 4);
	}

	bt_table_set(context, tshp->as.table_shape.layout, name, BT_VALUE_OBJECT(type));
}

void bt_tableshape_add_field(bt_Context* context, bt_Type* tshp, bt_Value name, bt_Value value, bt_Type* type)
{
	if (tshp->as.table_shape.values == 0) {
		tshp->as.table_shape.values = bt_make_table(context, 4);
		tshp->as.table_shape.proto = bt_make_table(context, 4);
	}

	bt_table_set(context, tshp->as.table_shape.proto, name, BT_VALUE_OBJECT(type));
	bt_table_set(context, tshp->as.table_shape.values, name, value);
}

void bt_tableshape_set_field(bt_Context* context, bt_Type* tshp, bt_Value name, bt_Value value)
{
	if (tshp->as.table_shape.values == 0) {
		tshp->as.table_shape.values = bt_make_table(context, 4);
		tshp->as.table_shape.proto = bt_make_table(context, 4);
	}

	bt_table_set(context, tshp->as.table_shape.values, name, value);
}

bt_Type* bt_make_union(bt_Context* context)
{
	bt_Type* result = bt_make_type(context, "<union>", bt_type_satisfier_union, BT_TYPE_CATEGORY_UNION, BT_FALSE);
	result->as.selector.types = BT_BUFFER_NEW(context, bt_Type*);

	return result;
}

void bt_push_union_variant(bt_Context* context, bt_Type* uni, bt_Type* variant)
{
	if (variant == 0) __debugbreak();
	bt_buffer_push(context, &uni->as.selector.types, &variant);
}

void bt_tableshape_set_proto(bt_Context* context, bt_Type* tshp, bt_Table* proto)
{
	if (tshp->as.table_shape.values == 0) {
		tshp->as.table_shape.values = bt_make_table(context, 4);
		tshp->as.table_shape.proto = bt_make_table(context, 4);
	}

	tshp->as.table_shape.values->prototype = proto;
}

bt_bool bt_is_type(bt_Value value, bt_Type* type)
{
	if (type == type->ctx->types.any) return BT_TRUE;
	if (type == type->ctx->types.null && value == BT_VALUE_NULL) return BT_TRUE;
	if (value == BT_VALUE_NULL) return BT_FALSE;
	if (type == type->ctx->types.boolean && BT_IS_BOOL(value)) return BT_TRUE;
	if (type == type->ctx->types.number && BT_IS_NUMBER(value)) return BT_TRUE;
	if (type == type->ctx->types.string && BT_IS_OBJECT(value) && BT_AS_OBJECT(value)->type == BT_OBJECT_TYPE_STRING) return BT_TRUE;

	if (!BT_IS_OBJECT(value)) return BT_FALSE;

	bt_Object* as_obj = BT_AS_OBJECT(value);

	switch (type->category) {
	case BT_TYPE_CATEGORY_TYPE:
		return as_obj->type == BT_OBJECT_TYPE_TYPE;
	case BT_TYPE_CATEGORY_SIGNATURE:
		if (as_obj->type == BT_OBJECT_TYPE_FN) {
			bt_Fn* as_fn = as_obj;
			return type->satisfier(type, as_fn->signature);
		}
		else if (as_obj->type == BT_OBJECT_TYPE_CLOSURE) {
			bt_Closure* cl = as_obj;
			return type->satisfier(type, cl->fn->signature);
		}
		else {
			return BT_FALSE;
		}
	case BT_TYPE_CATEGORY_TABLESHAPE: {
		bt_Table* as_tbl = as_obj;

		while (type) {
			bt_Buffer* layout = &type->as.table_shape.layout;
			for (uint32_t i = 0; i < layout->length; i++) {
				bt_TablePair* pair = bt_buffer_at(layout, i);

				bt_Value val = bt_table_get(as_tbl, pair->key);
				if (val == BT_VALUE_NULL) return BT_FALSE;
				if (!bt_is_type(val, pair->value)) return BT_FALSE;
			}

			type = type->as.table_shape.parent;
		}

		return BT_TRUE;
	} break;
	}

	// TODO: Table and array types
	return BT_FALSE;
}

bt_Value bt_cast_type(bt_Value value, bt_Type* type)
{
	// TODO: Actual dynamic casting
	return value;
}
