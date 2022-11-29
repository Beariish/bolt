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
