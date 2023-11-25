#include "bt_type.h"
#include "bt_context.h"

#include <memory.h>
#include <string.h>
#include <assert.h>

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
		bt_Type* arg_left = left->as.fn.args.elements[i];
		bt_Type* arg_right = right->as.fn.args.elements[i];
		
		if (!arg_right->satisfier(arg_right, arg_left))
			return BT_FALSE;
	}

	uint32_t n_unnamed_args = left->as.fn.args.length - n_typed_args;
	for (uint32_t i = 0; i < n_unnamed_args; ++i) {
		bt_Type* arg_left = left->as.fn.args.elements[n_typed_args + i];
		bt_Type* arg_right = right->as.fn.varargs_type;
	
		if (!arg_right->satisfier(arg_right, arg_left))
			return BT_FALSE;
	}

	return BT_TRUE;
}

bt_bool bt_is_optional(bt_Type* type)
{
	if (!type) return BT_FALSE;
	if (type == type->ctx->types.null) return BT_TRUE;

	if (type->category == BT_TYPE_CATEGORY_UNION) {
		bt_TypeBuffer* types = &type->as.selector.types;
	
		for (uint32_t i = 0; i < types->length; ++i) {
			bt_Type* inner = types->elements[i];
			if (inner == type->ctx->types.null) return BT_TRUE;
		}
	}

	return BT_FALSE;
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
	if (left == right) return BT_TRUE;

	if (left->category != BT_TYPE_CATEGORY_TABLESHAPE || right->category != BT_TYPE_CATEGORY_TABLESHAPE) return BT_FALSE;

	if (right->as.table_shape.parent) {
		if (bt_type_satisfier_table(left, right->as.table_shape.parent)) {
			return BT_TRUE;
		}
	}

	if (left->as.table_shape.sealed && right->as.table_shape.layout->length != left->as.table_shape.layout->length) return BT_FALSE;

	if (left->prototype_values &&
		left->prototype_values != right->prototype_values) {
		return BT_FALSE;
	}

	// Make sure that empty unsealed "{}" table binds to everything
	if (left->as.table_shape.layout) {
		bt_Table* lpairs = left->as.table_shape.layout;
		bt_Table* rpairs = right->as.table_shape.layout;
	
		for (uint32_t i = 0; i < lpairs->length; ++i) {
			bt_TablePair* lentry = BT_TABLE_PAIRS(lpairs) + i;

			bt_bool found = BT_FALSE;
			for (uint32_t j = 0; j < rpairs->length; ++j) {
				bt_TablePair* rentry = BT_TABLE_PAIRS(rpairs) + j;

				bt_Type* ltype = (bt_Type*)BT_AS_OBJECT(lentry->value);
				bt_Type* rtype = (bt_Type*)BT_AS_OBJECT(rentry->value);

				if (bt_value_is_equal(lentry->key, rentry->key) &&
					ltype->satisfier(ltype, rtype)) {
					found = BT_TRUE;
					break;
				}
			}

			if (found == BT_FALSE) return BT_FALSE;
		}
	}

	return BT_TRUE;
}

static bt_bool bt_type_satisfier_map(bt_Type* left, bt_Type* right)
{
	if (left->category != BT_TYPE_CATEGORY_TABLESHAPE || right->category != BT_TYPE_CATEGORY_TABLESHAPE) return BT_FALSE;
	
	bt_Type* l_key = left->as.table_shape.key_type;
	bt_Type* l_val = left->as.table_shape.value_type;
	
	if (left->as.table_shape.map != right->as.table_shape.map) {
		bt_Table* keys = right->as.table_shape.key_layout;
		bt_Table* vals = right->as.table_shape.layout;

		for (uint32_t i = 0; i < keys->length; ++i) {
			bt_Type* key_type = (bt_Type*)BT_AS_OBJECT((BT_TABLE_PAIRS(keys) + i)->value);
			bt_Type* val_type = (bt_Type*)BT_AS_OBJECT((BT_TABLE_PAIRS(vals) + i)->value);
		
			if (l_key->satisfier(l_key, key_type) == BT_FALSE) return BT_FALSE;
			if (l_val->satisfier(l_val, val_type) == BT_FALSE) return BT_FALSE;
		}

		return BT_TRUE;
	}

	return l_key->satisfier(l_key, right->as.table_shape.key_type) && l_val->satisfier(l_val, right->as.table_shape.value_type);
}

bt_bool bt_type_satisfier_union(bt_Type* left, bt_Type* right)
{
	if (!left || !right) return BT_FALSE;
	if (left->category != BT_TYPE_CATEGORY_UNION) return BT_FALSE;

	bt_TypeBuffer* types = &left->as.selector.types;

	if (right->category == BT_TYPE_CATEGORY_UNION) {
		bt_TypeBuffer* rtypes = &right->as.selector.types;
		for (uint32_t i = 0; i < rtypes->length; ++i) {
			bt_Type* rtype = rtypes->elements[i];
			
			bt_bool found = BT_FALSE;

			for (uint32_t j = 0; j < types->length; ++j) {
				bt_Type* type = types->elements[j];
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
			bt_Type* type = types->elements[i];
			if (type->satisfier(type, right)) {
				return BT_TRUE;
			}
		}
	}

	return BT_FALSE;
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

bt_Type* bt_make_type(bt_Context* context, const char* name, bt_TypeSatisfier satisfier, bt_TypeCategory category)
{
	bt_Type* result = BT_ALLOCATE(context, TYPE, bt_Type);
	result->ctx = context;
	
	if (name) {
		result->name = context->alloc(strlen(name) + 1);
		strcpy(result->name, name);
	}

	result->satisfier = satisfier;
	result->category = category;
	result->is_polymorphic = BT_FALSE;
	result->prototype = 0;
	result->prototype_types = 0;
	result->prototype_values = 0;

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
	bt_Type* new_type = bt_make_union(context);
	bt_push_union_variant(context, new_type, to_nullable);
	bt_push_union_variant(context, new_type, context->types.null);

	return new_type;
}

bt_Type* bt_remove_nullable(bt_Context* context, bt_Type* to_unnull) {
	assert(to_unnull->category == BT_TYPE_CATEGORY_UNION);

	int32_t found_idx = -1;
	bt_TypeBuffer* types = &to_unnull->as.selector.types;

	for (uint32_t i = 0; i < types->length; i++) {
		if (types->elements[i] == context->types.null) {
			found_idx = i;
		}
	}

	assert(found_idx >= 0);
	assert(types->length > 1);

	// fast path for regular optionals!
	if (types->length == 2) {
		return types->elements[1 - found_idx];
	}

	bt_Type* result = bt_make_union(context);
	for (uint32_t i = 0; i < types->length; i++) {
		if (i == found_idx) continue;

		bt_push_union_variant(context, result, types->elements[i]);
	}

	return result;
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
		bt_Type* arg = fn->as.fn.args.elements[i];
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
	bt_Type* result = bt_make_type(context, "", bt_type_satisfier_signature, BT_TYPE_CATEGORY_SIGNATURE);
	result->as.fn.return_type = ret;
	bt_buffer_with_capacity(&result->as.fn.args, context, arg_count);
	for (uint8_t i = 0; i < arg_count; ++i) bt_buffer_push(context, &result->as.fn.args, args[i]);
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
	bt_Type* result = bt_make_type(context, name, type_satisfier_alias, BT_TYPE_CATEGORY_TYPE);
	result->as.type.boxed = boxed;

	return result;
}

bt_Type* bt_make_fundamental(bt_Context* context)
{
	return bt_make_type(context, "Type", type_satisfier_type, BT_TYPE_CATEGORY_TYPE);
}

bt_Type* bt_make_userdata_type(bt_Context* context, const char* name)
{
	bt_Type* result = bt_make_type(context, name, bt_type_satisfier_same, BT_TYPE_CATEGORY_USERDATA);
	bt_buffer_empty(&result->as.userdata.fields);
	bt_buffer_empty(&result->as.userdata.functions);
	return result;
}

bt_Type* bt_make_poly_signature(bt_Context* context, const char* name, bt_PolySignature applicator)
{
	bt_Type* result = bt_make_type(context, name, bt_type_satisfier_same, BT_TYPE_CATEGORY_SIGNATURE);
	result->as.poly_fn.applicator = applicator;
	result->is_polymorphic = BT_TRUE;

	return result;
}

bt_Type* bt_make_poly_method(bt_Context* context, const char* name, bt_PolySignature applicator)
{
	bt_Type* result = bt_make_poly_signature(context, name, applicator);
	result->as.fn.is_method = BT_TRUE;
	return result;
}

bt_Type* bt_make_tableshape(bt_Context* context, const char* name, bt_bool sealed)
{
	bt_Type* result = bt_make_type(context, name, bt_type_satisfier_table, BT_TYPE_CATEGORY_TABLESHAPE);
	result->prototype = context->types.table;
	result->as.table_shape.sealed = sealed;
	result->as.table_shape.layout = 0;
	result->as.table_shape.parent = 0;
	result->as.table_shape.map = 0;
	result->as.table_shape.tmpl = 0;
	return result;
}

void bt_tableshape_add_layout(bt_Context* context, bt_Type* tshp, bt_Type* key_type, bt_Value key, bt_Type* type)
{
	if (tshp->as.table_shape.layout == 0) {
		tshp->as.table_shape.layout = bt_make_table(context, 4);
		tshp->as.table_shape.key_layout = bt_make_table(context, 4);
	}

	bt_table_set(context, tshp->as.table_shape.layout, key, BT_VALUE_OBJECT(type));
	bt_table_set(context, tshp->as.table_shape.key_layout, key, BT_VALUE_OBJECT(key_type));
}

bt_Type* bt_tableshape_get_layout(bt_Type* tshp, bt_Value key)
{
	if (tshp->as.table_shape.layout == 0) return NULL;

	bt_Value result = bt_table_get(tshp->as.table_shape.layout, key);
	if (result == BT_VALUE_NULL) return NULL;

	return (bt_Type*)BT_AS_OBJECT(result);
}

void bt_type_add_field(bt_Context* context, bt_Type* tshp, bt_Type* type, bt_Value name, bt_Value value)
{
	if (tshp->prototype_values == 0) {
		tshp->prototype_values = bt_make_table(context, 4);
		tshp->prototype_types = bt_make_table(context, 4);
	}

	bt_table_set(context, tshp->prototype_types, name, BT_VALUE_OBJECT(type));
	bt_table_set(context, tshp->prototype_values, name, value);
}

void bt_type_set_field(bt_Context* context, bt_Type* tshp, bt_Value name, bt_Value value)
{
	if (tshp->prototype_values == 0) {
		tshp->prototype_values = bt_make_table(context, 4);
		tshp->prototype_types = bt_make_table(context, 4);
	}

	bt_table_set(context, tshp->prototype_values, name, value);
}

bt_Type* bt_make_array_type(bt_Context* context, bt_Type* inner)
{
	bt_Type* result = bt_make_type(context, "array", bt_type_satisfier_array, BT_TYPE_CATEGORY_ARRAY);
	result->as.array.inner = inner;
	result->prototype = context->types.array;
	return result;
}

void bt_tableshape_set_parent(bt_Context* context, bt_Type* tshp, bt_Type* parent)
{
	tshp->as.table_shape.parent = parent;

	if (tshp->prototype_values == 0) {
		tshp->prototype_values = bt_make_table(context, 4);
		tshp->prototype_types = bt_make_table(context, 4);
	}

	tshp->prototype_types->prototype = parent->prototype_types;
	tshp->prototype_values->prototype = parent->prototype_values;
}

bt_Type* bt_make_map(bt_Context* context, bt_Type* key, bt_Type* value)
{
	bt_Type* result = bt_make_type(context, "map", bt_type_satisfier_map, BT_TYPE_CATEGORY_TABLESHAPE);
	result->as.table_shape.sealed = 0;
	result->as.table_shape.layout = 0;
	result->as.table_shape.parent = 0;
	result->as.table_shape.map = 1;

	result->as.table_shape.key_type = key;
	result->as.table_shape.value_type = value;

	return result;
}

bt_Table* bt_type_get_proto(bt_Context* context, bt_Type* tshp)
{
	if (tshp->prototype_values == 0 && tshp->as.table_shape.parent) {
		tshp->prototype_values = bt_make_table(context, 4);
		tshp->prototype_types = bt_make_table(context, 4);
	}

	if (tshp->as.table_shape.parent) {
		tshp->prototype_values->prototype = tshp->as.table_shape.parent->prototype_values;
	}

	return tshp->prototype_values;
}

bt_Type* bt_make_union(bt_Context* context)
{
	bt_Type* result = bt_make_type(context, "<union>", bt_type_satisfier_union, BT_TYPE_CATEGORY_UNION);
	bt_buffer_empty(&result->as.selector.types);
	return result;
}

void bt_push_union_variant(bt_Context* context, bt_Type* uni, bt_Type* variant)
{
	bt_buffer_push(context, &uni->as.selector.types, variant);

	// todo(bearish): don't like this
	uint32_t new_length = 0;
	for (uint32_t i = 0; i < uni->as.selector.types.length; ++i) {
		if (i != 0) new_length += 3; // " | "
		char* name = uni->as.selector.types.elements[i]->name;
		if (name) new_length += (uint32_t)strlen(name);
		else new_length += 1; // "?";
	}

	uni->name = context->realloc(uni->name, new_length + 1);

	uint32_t written_length = 0;

	for (uint32_t i = 0; i < uni->as.selector.types.length; ++i) {
		if (i != 0) { memcpy(uni->name + written_length, " | ", 3); written_length += 3; }

		char* name = uni->as.selector.types.elements[i]->name;
		if (name) {
			uint32_t name_len = (uint32_t)strlen(name);
			memcpy(uni->name + written_length, name, name_len);
			written_length += name_len;
		}
		else uni->name[written_length++] = '?';
	}

	uni->name[written_length] = 0;
}

BOLT_API bt_bool bt_union_has_variant(bt_Type* uni, bt_Type* variant)
{
	for (uint32_t i = 0; i < uni->as.selector.types.length; ++i) {
		if (uni->as.selector.types.elements[i] == variant) return BT_TRUE;
	}

	return BT_FALSE;
}

bt_Type* bt_make_enum(bt_Context* context, bt_StrSlice name)
{
	bt_String* owned_name = bt_make_string_hashed_len(context, name.source, name.length);
	bt_Type* result = bt_make_type(context, BT_STRING_STR(owned_name), bt_type_satisfier_same, BT_TYPE_CATEGORY_ENUM);
	result->as.enum_.name = owned_name;
	result->as.enum_.options = bt_make_table(context, 0);

	return result;
}

void bt_enum_push_option(bt_Context* context, bt_Type* enum_, bt_StrSlice name, bt_Value value)
{
	bt_String* owned_name = bt_make_string_hashed_len(context, name.source, name.length);

	bt_table_set(context, enum_->as.enum_.options, BT_VALUE_OBJECT(owned_name), value);
}

bt_Value bt_enum_contains(bt_Context* context, bt_Type* enum_, bt_Value value)
{
	bt_Table* pairs = enum_->as.enum_.options;
	for (uint32_t i = 0; i < pairs->length; i++) {
		if (bt_value_is_equal((BT_TABLE_PAIRS(pairs) + i)->value, value)) {
			return (BT_TABLE_PAIRS(pairs) + i)->key;
		}
	}

	return BT_VALUE_NULL;
}

bt_Value bt_enum_get(bt_Context* context, bt_Type* enum_, bt_String* name)
{
	return bt_table_get(enum_->as.enum_.options, BT_VALUE_OBJECT(name));
}

bt_Type* bt_type_dealias(bt_Type* type)
{
	if (type && type->category == BT_TYPE_CATEGORY_TYPE) return bt_type_dealias(type->as.type.boxed);
	return type;
}

bt_bool bt_is_alias(bt_Type* type)
{
	return type->satisfier == type_satisfier_alias;
}

bt_bool bt_is_type(bt_Value value, bt_Type* type)
{
	if (type == type->ctx->types.any) return BT_TRUE;
	if (type == type->ctx->types.null && value == BT_VALUE_NULL) return BT_TRUE;
	if (value == BT_VALUE_NULL) return BT_FALSE;
	if (type == type->ctx->types.boolean && BT_IS_BOOL(value)) return BT_TRUE;
	if (type == type->ctx->types.number && BT_IS_NUMBER(value)) return BT_TRUE;

	if (!BT_IS_OBJECT_FAST(value)) return BT_FALSE;
	bt_Object* as_obj = BT_AS_OBJECT(value);

	if (type == type->ctx->types.string && BT_OBJECT_GET_TYPE(as_obj) == BT_OBJECT_TYPE_STRING) return BT_TRUE;

	switch (type->category) {
	case BT_TYPE_CATEGORY_TYPE:
		return BT_OBJECT_GET_TYPE(as_obj) == BT_OBJECT_TYPE_TYPE;
	case BT_TYPE_CATEGORY_SIGNATURE:
		if (BT_OBJECT_GET_TYPE(as_obj) == BT_OBJECT_TYPE_FN) {
			bt_Fn* as_fn = (bt_Fn*)as_obj;
			return type->satisfier(type, as_fn->signature);
		}
		else if (BT_OBJECT_GET_TYPE(as_obj) == BT_OBJECT_TYPE_CLOSURE) {
			bt_Closure* cl = (bt_Closure*)as_obj;
			return type->satisfier(type, cl->fn->signature);
		}
		else {
			return BT_FALSE;
		}
	case BT_TYPE_CATEGORY_TABLESHAPE: {
		bt_Table* as_tbl = (bt_Table*)as_obj;

		while (type) {
			bt_Table* layout = type->as.table_shape.layout;
			for (uint32_t i = 0; i < layout->length; i++) {
				bt_TablePair* pair = BT_TABLE_PAIRS(layout) + i;

				bt_Value val = bt_table_get(as_tbl, pair->key);
				if (val == BT_VALUE_NULL) return BT_FALSE;
				if (!bt_is_type(val, (bt_Type*)BT_AS_OBJECT(pair->value))) return BT_FALSE;
			}

			type = type->as.table_shape.parent;
		}

		return BT_TRUE;
	} break;
	}

	// TODO: Table and array types
	return BT_FALSE;
}

bt_bool bt_satisfies_type(bt_Value value, bt_Type* type)
{
	if (type->category == BT_TYPE_CATEGORY_TABLESHAPE) {
		bt_Object* obj = BT_AS_OBJECT(value);
		if (BT_OBJECT_GET_TYPE(obj) != BT_OBJECT_TYPE_TABLE) {
			return BT_FALSE;
		}

		bt_Table* src = (bt_Table*)obj;
		bt_Table* layout = type->as.table_shape.layout;

		for (uint32_t i = 0; i < layout->length; ++i) {
			bt_TablePair* pair = BT_TABLE_PAIRS(layout) + i;

			bt_Value val = bt_table_get(src, pair->key);

			if (val == BT_VALUE_NULL && bt_is_optional((bt_Type*)BT_AS_OBJECT(pair->value)) == BT_FALSE) {
				return BT_FALSE;
			}
		}

		return BT_TRUE;
	}

	return bt_is_type(value, type);
}

bt_Value bt_cast_type(bt_Value value, bt_Type* type)
{
	if (type == type->ctx->types.string) {
		return BT_VALUE_OBJECT(bt_to_string(type->ctx, value));
	}

	if (type->category == BT_TYPE_CATEGORY_TABLESHAPE) {
		if (!BT_IS_OBJECT_FAST(value)) return BT_VALUE_NULL;

		bt_Object* obj = BT_AS_OBJECT(value);
		if (BT_OBJECT_GET_TYPE(obj) != BT_OBJECT_TYPE_TABLE) {
			bt_runtime_error(type->ctx->current_thread, "lhs was not a table!", NULL);
		}

		bt_Table* src = (bt_Table*)obj;
		bt_Table* layout = type->as.table_shape.layout;
		
		bt_Table* dst = bt_make_table(type->ctx, layout->length);

		for (uint32_t i = 0; i < layout->length; ++i) {
			bt_TablePair* pair = BT_TABLE_PAIRS(layout) + i;

			bt_Value val = bt_table_get(src, pair->key);

			if (val == BT_VALUE_NULL && bt_is_optional((bt_Type*)BT_AS_OBJECT(pair->value)) == BT_FALSE) {
				bt_runtime_error(type->ctx->current_thread, "Missing field in table type!", NULL);
			}

			bt_table_set(type->ctx, dst, pair->key, val);
		}

		dst->prototype = bt_type_get_proto(type->ctx, type);
		return BT_VALUE_OBJECT(dst);
	}

	if (bt_is_type(value, type)) { return value; }

	return BT_VALUE_NULL;
}

BOLT_API bt_bool bt_type_is_equal(bt_Type* a, bt_Type* b)
{
	if (!a && !b) return BT_TRUE;
	if (!a || !b) return BT_FALSE;

	a = bt_type_dealias(a); b = bt_type_dealias(b);
	if (a == b) return BT_TRUE;
	if (a->category != b->category) return BT_FALSE;

	switch (a->category) {
	case BT_TYPE_CATEGORY_ARRAY: return bt_type_is_equal(a->as.array.inner, b->as.array.inner);
	case BT_TYPE_CATEGORY_TABLESHAPE: 
		if (a->prototype_values) return a->prototype_values == b->prototype_values;
		else return BT_FALSE;
	case BT_TYPE_CATEGORY_SIGNATURE:
		if (a->is_polymorphic) {
			if (!b->is_polymorphic) return BT_FALSE;
			return a->as.poly_fn.applicator == b->as.poly_fn.applicator;
		}

		if (a->as.fn.is_method != b->as.fn.is_method || a->as.fn.is_vararg != b->as.fn.is_vararg) return BT_FALSE;
		if (a->as.fn.is_vararg && !bt_type_is_equal(a->as.fn.varargs_type, b->as.fn.varargs_type)) return BT_FALSE;
		if (!bt_type_is_equal(a->as.fn.return_type, b->as.fn.return_type)) return BT_FALSE;

		if (a->as.fn.args.length != b->as.fn.args.length) return BT_FALSE;
		for (uint32_t i = 0; i < a->as.fn.args.length; ++i) {
			bt_Type* arg_a = a->as.fn.args.elements[i];
			bt_Type* arg_b = b->as.fn.args.elements[i];
			if (!bt_type_is_equal(arg_a, arg_b)) return BT_FALSE;
		}

		return BT_TRUE;
	case BT_TYPE_CATEGORY_UNION:
		if (a->as.selector.types.length != b->as.selector.types.length) return BT_FALSE;
		for (uint32_t i = 0; i < a->as.selector.types.length; ++i) {
			bt_Type* a_current = a->as.selector.types.elements[i];
			bt_bool found = BT_FALSE;
			for (uint32_t j = 0; j < b->as.selector.types.length; ++i) {
				bt_Type* b_current = b->as.selector.types.elements[i];
				if (bt_type_is_equal(a_current, b_current)) {
					found = BT_TRUE;
					break;
				}
			}

			if (!found) return BT_FALSE;
		}

		return BT_TRUE;
	}

	return BT_FALSE;
}

bt_Type* bt_type_any(bt_Context* context) { return context->types.any; }
bt_Type* bt_type_null(bt_Context* context) { return context->types.null; }
bt_Type* bt_type_number(bt_Context* context) { return context->types.number; }
bt_Type* bt_type_boolean(bt_Context* context) { return context->types.boolean; }
bt_Type* bt_type_string(bt_Context* context) { return context->types.string; }
bt_Type* bt_type_array(bt_Context* context) { return context->types.array; }
bt_Type* bt_type_table(bt_Context* context) { return context->types.table; }
bt_Type* bt_type_fn(bt_Context* context) { return context->types.fn; }
bt_Type* bt_type_type(bt_Context* context) { return context->types.type; }