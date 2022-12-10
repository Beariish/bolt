#include "bt_type.h"
#include "bt_context.h"

#define _CRT_SECURE_NO_WARNINGS

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
	if (left->as.table_shape.sealed != right->as.table_shape.sealed) return BT_FALSE;

	if (left->as.table_shape.proto &&
		left->as.table_shape.proto != right->as.table_shape.proto) {
		return BT_FALSE;
	}

	bt_Buffer* lpairs = &left->as.table_shape.layout->pairs;
	bt_Buffer* rpairs = &right->as.table_shape.layout->pairs;
	
	for (uint32_t i = 0; i < lpairs->length; ++i) {
		bt_TablePair* lentry = bt_buffer_at(lpairs, i);

		bt_bool found = BT_FALSE;
		for (uint32_t j = 0; j < rpairs->length; ++j) {
			bt_TablePair* rentry = bt_buffer_at(rpairs, j);

			if (bt_value_is_equal(lentry->key, rentry->key) &&
				bt_value_is_equal(lentry->value, rentry->value)) {
				found = BT_TRUE;
				break;
			}
		}

		if (found == BT_FALSE) return BT_FALSE;
	}

	return BT_TRUE;
}

bt_bool type_satisifer_nullable(bt_Type* left, bt_Type* right)
{
	return bt_type_satisfier_same(left->as.nullable.base, right) ||
		(left->is_optional && right == left->ctx->types.null);
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

bt_Type* bt_make_signature(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count)
{	
	bt_Type* result = bt_make_type(context, "", bt_type_satisfier_same, BT_TYPE_CATEGORY_SIGNATURE, BT_FALSE);
	result->as.fn.return_type = ret;
	result->as.fn.args = BT_BUFFER_WITH_CAPACITY(context, bt_Type*, arg_count);

	context->free(result->name);


	// TODO(bearish): For the absolute fucking love of god please rewrite this. im begging you. thanks.
	uint16_t name_len = 0;
	name_len += strlen("fn (");
	for (uint8_t i = 0; i < arg_count; i++) {
		bt_Type* arg = args[i];
		bt_buffer_push(context, &result->as.fn.args, &arg);
		name_len += strlen(arg->name);
		if (i < arg_count - 1)
			name_len += strlen(", ");
	}

	name_len += strlen(")");
	if (ret) {
		name_len += strlen(": ");
		name_len += strlen(ret->name);
	}

	char* new_name = context->alloc(name_len + 1);
	char* new_name_base = new_name;

	strcpy(new_name, "fn (");
	new_name += strlen("fn (");

	for (uint8_t i = 0; i < arg_count; i++) {
		bt_Type* arg = args[i];
		strcpy(new_name, arg->name);
		new_name += strlen(arg->name);
		if (i < arg_count - 1) {
			strcpy(new_name, ", ");
			new_name += strlen(", ");
		}
	}

	strcpy(new_name, ")");
	new_name += strlen(")");

	if (ret) {
		strcpy(new_name, ": ");
		new_name += strlen(": ");
		strcpy(new_name, ret->name);
		new_name += strlen(ret->name);
	}

	result->name = new_name_base;

	return result;
}

bt_Type* bt_make_tableshape(bt_Context* context, const char* name, bt_bool sealed)
{
	bt_Type* result = bt_make_type(context, name, bt_type_satisfier_table, BT_TYPE_CATEGORY_TABLESHAPE, BT_FALSE);
	result->as.table_shape.sealed = sealed;
	result->as.table_shape.layout = 0;
	result->as.table_shape.proto = 0;
	return result;
}

void bt_tableshape_add_field(bt_Context* context, bt_Type* tshp, bt_Value name, bt_Type* type)
{
	if (tshp->as.table_shape.layout == 0) {
		tshp->as.table_shape.layout = bt_make_table(context, 1);
	}

	bt_table_set(context, tshp->as.table_shape.layout, name, BT_VALUE_OBJECT(type));
}

void bt_tableshape_set_proto(bt_Type* tshp, bt_Table* proto)
{
	tshp->as.table_shape.proto = proto;
}
