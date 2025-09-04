#include "bt_type.h"
#include "bt_context.h"

#include <string.h>
#include <assert.h>

static bt_bool bt_type_satisfier_signature(bt_Type* left, bt_Type* right)
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
		
		if (!arg_left->satisfier(arg_left, arg_right))
			return BT_FALSE;
	}

	uint32_t n_unnamed_args = left->as.fn.args.length - n_typed_args;
	for (uint32_t i = 0; i < n_unnamed_args; ++i) {
		bt_Type* arg_left = left->as.fn.args.elements[n_typed_args + i];
		bt_Type* arg_right = right->as.fn.varargs_type;
	
		if (!arg_left->satisfier(arg_left, arg_right))
			return BT_FALSE;
	}

	return BT_TRUE;
}

bt_bool bt_type_is_optional(bt_Type* type)
{
	if (!type) return BT_FALSE;
	if (type == type->ctx->types.null) return BT_TRUE;
	if (type == type->ctx->types.any) return BT_TRUE;
	
	return bt_union_has_variant(type, bt_type_null(type->ctx)) != -1;
}

static bt_bool bt_type_satisfier_array(bt_Type* left, bt_Type* right)
{
	return bt_type_satisfier_same(left, right) || (
		(left->category == BT_TYPE_CATEGORY_ARRAY && left->category == right->category) &&
		left->as.array.inner->satisfier(
			left->as.array.inner,
			right->as.array.inner));
}

static bt_bool bt_type_satisfier_table(bt_Type* left, bt_Type* right)
{
	if (left == right) return BT_TRUE;

	if (left->category != BT_TYPE_CATEGORY_TABLESHAPE || right->category != BT_TYPE_CATEGORY_TABLESHAPE) return BT_FALSE;

	if (right->as.table_shape.parent) {
		if (bt_type_satisfier_table(left, right->as.table_shape.parent)) {
			return BT_TRUE;
		}
	}

	uint32_t left_length = left->as.table_shape.layout ? left->as.table_shape.layout->length : 0;
	uint32_t right_length = right->as.table_shape.layout ? right->as.table_shape.layout->length : 0;
	if (left->as.table_shape.sealed && left_length != right_length) return BT_FALSE;

	if (left->prototype_values &&
		left->prototype_values != right->prototype_values) {
		return BT_FALSE;
	}

	// Make sure that empty unsealed "{}" table binds to everything
	if (left->as.table_shape.layout) {
		bt_Table* lpairs = left->as.table_shape.layout;
		bt_Table* rpairs = right->as.table_shape.layout;
	
		for (uint32_t i = 0; i < (lpairs ? lpairs->length : 0u); ++i) {
			bt_TablePair* lentry = BT_TABLE_PAIRS(lpairs) + i;

			bt_bool found = BT_FALSE;
			for (uint32_t j = 0; j < (rpairs ? rpairs->length : 0u); ++j) {
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

		if (keys) {
			for (uint32_t i = 0; i < keys->length; ++i) {
				bt_Type* key_type = (bt_Type*)BT_AS_OBJECT((BT_TABLE_PAIRS(keys) + i)->value);
				bt_Type* val_type = (bt_Type*)BT_AS_OBJECT((BT_TABLE_PAIRS(vals) + i)->value);
		
				if (l_key->satisfier(l_key, key_type) == BT_FALSE) return BT_FALSE;
				if (l_val->satisfier(l_val, val_type) == BT_FALSE) return BT_FALSE;
			}
		}

		return BT_TRUE;
	}

	return l_key->satisfier(l_key, right->as.table_shape.key_type) && l_val->satisfier(l_val, right->as.table_shape.value_type);
}

static bt_bool bt_type_satisfier_union(bt_Type* left, bt_Type* right)
{
	if (!left || !right) return BT_FALSE;
	if (left->category != BT_TYPE_CATEGORY_UNION) return BT_FALSE;
	if (left == right) return BT_TRUE;

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

static bt_bool type_satisfier_alias(bt_Type* left, bt_Type* right)
{
	if (right->category == BT_TYPE_CATEGORY_TYPE) {
		return left->as.type.boxed->satisfier(left->as.type.boxed, right->as.type.boxed);
	}

	return left->as.type.boxed->satisfier(left->as.type.boxed, right);
}

static bt_bool type_satisfier_type(bt_Type* left, bt_Type* right)
{
	return right->category == BT_TYPE_CATEGORY_TYPE;
}

static bt_Type* bt_make_type(bt_Context* context, const char* name, bt_TypeSatisfier satisfier, bt_TypeCategory category)
{
	bt_Type* result = BT_ALLOCATE(context, TYPE, bt_Type);
	result->ctx = context;
	
	if (name) {
		result->name = bt_gc_alloc(context, strlen(name) + 1);
		strcpy(result->name, name);
	}

	result->satisfier = satisfier;
	result->category = category;
	result->is_polymorphic = BT_FALSE;
	result->prototype = 0;
	result->prototype_types = 0;
	result->prototype_values = 0;
	result->annotations = NULL;
	
	return result;
}

bt_Type* bt_make_primitive_type(bt_Context* ctx, const char* name, bt_TypeSatisfier satisfier)
{
	return bt_make_type(ctx, name, satisfier, BT_TYPE_CATEGORY_PRIMITIVE);
}

bt_Type* bt_type_make_nullable(bt_Context* context, bt_Type* to_nullable)
{
	// Special casing for nullable null to avoid redundant unions
	if (to_nullable == context->types.null) return to_nullable;
	if (to_nullable == context->types.any) return to_nullable;
	if (bt_type_is_optional(to_nullable)) return to_nullable;

	return bt_make_or_extend_union(context, to_nullable, bt_type_null(context));
}

bt_Type* bt_type_remove_nullable(bt_Context* context, bt_Type* to_unnull) {
	if (to_unnull->category != BT_TYPE_CATEGORY_UNION) return to_unnull;

	int32_t found_idx = -1;
	bt_TypeBuffer* types = &to_unnull->as.selector.types;

	for (uint32_t i = 0; i < types->length; i++) {
		if (types->elements[i] == context->types.null) {
			found_idx = i;
		}
	}

	if (found_idx < 0 || types->length <= 1) return  to_unnull;

	// fast path for regular optionals!
	if (types->length == 2) {
		return types->elements[1 - found_idx];
	}

	bt_Type* result = bt_make_union(context);
	for (uint32_t i = 0; i < types->length; i++) {
		if (i == found_idx) continue;

		bt_union_push_variant(context, result, types->elements[i]);
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

	if (fn->name) bt_gc_free(ctx, fn->name, strlen(fn->name) + 1);

	char* new_name = bt_gc_alloc(ctx, name_buf_cur - name_buf_base + 1);
	memcpy(new_name, name_buf_base, name_buf_cur - name_buf_base);
	new_name[name_buf_cur - name_buf_base] = 0;

	fn->name = new_name;
}

bt_Type* bt_make_signature_type(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count)
{
	bt_Type* result = bt_make_type(context, "", bt_type_satisfier_signature, BT_TYPE_CATEGORY_SIGNATURE);
	result->as.fn.return_type = ret;
	bt_buffer_with_capacity(&result->as.fn.args, context, arg_count);
	for (uint8_t i = 0; i < arg_count; ++i) { bt_buffer_push(context, &result->as.fn.args, args[i]); }
	result->as.fn.is_vararg = BT_FALSE;
	result->as.fn.varargs_type = NULL;

	update_sig_name(context, result);

	return result;
}

bt_Type* bt_make_signature_vararg(bt_Context* context, bt_Type* original, bt_Type* varargs_type)
{
	original->as.fn.is_vararg = BT_TRUE;
	original->as.fn.varargs_type = varargs_type;

	update_sig_name(context, original);

	return original;
}

bt_bool bt_type_is_methodic(bt_Type* signature, bt_Type* t)
{
	if (t->category != BT_TYPE_CATEGORY_TABLESHAPE) return BT_FALSE;
	if (signature->category != BT_TYPE_CATEGORY_SIGNATURE) return BT_FALSE;
	if (signature->as.fn.args.length < 1) return BT_FALSE;

	return signature->as.fn.args.elements[0]->satisfier(signature->as.fn.args.elements[0], t);
}

bt_Type* bt_make_alias_type(bt_Context* context, const char* name, bt_Type* boxed)
{
	bt_Type* result = bt_make_type(context, name, type_satisfier_alias, BT_TYPE_CATEGORY_TYPE);
	result->as.type.boxed = boxed;

	return result;
}

bt_Type* bt_make_fundamental_type(bt_Context* context)
{
	return bt_make_type(context, "Type", type_satisfier_type, BT_TYPE_CATEGORY_TYPE);
}

bt_Type* bt_make_userdata_type(bt_Context* context, const char* name)
{
	bt_Type* result = bt_make_type(context, name, bt_type_satisfier_same, BT_TYPE_CATEGORY_USERDATA);
	bt_buffer_empty(&result->as.userdata.fields);
	result->as.userdata.finalizer = NULL;
	return result;
}

bt_Type* bt_make_poly_signature_type(bt_Context* context, const char* name, bt_PolySignature applicator)
{
	bt_Type* result = bt_make_type(context, name, bt_type_satisfier_same, BT_TYPE_CATEGORY_SIGNATURE);
	result->as.poly_fn.applicator = applicator;
	result->is_polymorphic = BT_TRUE;

	return result;
}

bt_Type* bt_make_tableshape_type(bt_Context* context, const char* name, bt_bool sealed)
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

bt_bool bt_type_get_field(bt_Context* context, bt_Type* tshp, bt_Value key, bt_Value* value) {
	if (tshp->category != BT_TYPE_CATEGORY_TABLESHAPE) return BT_FALSE;
	if (!tshp->prototype_values) return BT_FALSE;

	bt_Value type_value = bt_table_get(tshp->prototype_types, key);
	if (type_value == BT_VALUE_NULL) return BT_FALSE;

	bt_Type* type = (bt_Type*)BT_AS_OBJECT(type_value);
	bt_Value result = bt_table_get(tshp->prototype_values, key);
	if (!bt_is_type(result, type)) return BT_FALSE;

	if (value) *value = result;
	return BT_TRUE;
}

bt_Type* bt_type_get_field_type(bt_Context* context, bt_Type* tshp, bt_Value key)
{
	if (tshp->category != BT_TYPE_CATEGORY_TABLESHAPE) return BT_FALSE;
	if (!tshp->prototype_types) return BT_FALSE;

	bt_Value type_value = bt_table_get(tshp->prototype_types, key);
	if (type_value == BT_VALUE_NULL) return NULL;
	return (bt_Type*)BT_AS_OBJECT(type_value);
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

void bt_tableshape_set_field_annotations(bt_Context* context, bt_Type* tshp, bt_Value key, bt_Annotation* annotations)
{
	if (!tshp->as.table_shape.field_annotations) {
		tshp->as.table_shape.field_annotations = bt_make_table(context, 1);
	}

	bt_table_set(context, tshp->as.table_shape.field_annotations, key, BT_VALUE_OBJECT(annotations));
}

bt_Annotation* bt_tableshape_get_field_annotations(bt_Type* tshp, bt_Value key)
{
	if (!tshp->as.table_shape.field_annotations) return NULL;
	bt_Value result = bt_table_get(tshp->as.table_shape.field_annotations, key);
	if (result == BT_VALUE_NULL) return NULL; 
	return (bt_Annotation*)BT_AS_OBJECT(result);
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

bt_Type* bt_make_or_extend_union(bt_Context* context, bt_Type* uni, bt_Type* variant)
{
	if (!uni && (!variant || variant->category != BT_TYPE_CATEGORY_UNION)) return variant;
	if (uni == variant) return uni;
	if (!uni || uni->category != BT_TYPE_CATEGORY_UNION) {
		bt_Type* first = uni;
		uni = bt_make_union(context);
		if (first) bt_union_push_variant(context, uni, first);
	}

	bt_union_push_variant(context, uni, variant);
	return uni;
}

bt_Type* bt_make_union_from(bt_Context* context, bt_Type** types, size_t type_count) {
	bt_Type* result = NULL;
	for (size_t i = 0; i < type_count; i++) {
		result = bt_make_or_extend_union(context, result, types[i]);
	}

	return result;
}


void bt_union_push_variant(bt_Context* context, bt_Type* uni, bt_Type* variant)
{
	if (variant->category == BT_TYPE_CATEGORY_UNION) {
		for (uint32_t i = 0; i < variant->as.selector.types.length; ++i) {
			bt_Type* other_variant = variant->as.selector.types.elements[i];
			if (!bt_type_satisfier_union(uni, other_variant)) {
				bt_buffer_push(context, &uni->as.selector.types, other_variant);
			}
		}
	} else {
		// Stop us from adding duplicates
		for (uint32_t i = 0; i < uni->as.selector.types.length; ++i) {
			bt_Type* other_variant = uni->as.selector.types.elements[i];
			if (other_variant == variant) return;
		}
		
		bt_buffer_push(context, &uni->as.selector.types, variant);
	}

	// todo(bearish): don't like this
	uint32_t new_length = 0;
	for (uint32_t i = 0; i < uni->as.selector.types.length; ++i) {
		if (i != 0) new_length += 3; // " | "
		char* name = uni->as.selector.types.elements[i]->name;
		if (name) new_length += (uint32_t)strlen(name);
		else new_length += 1; // "?";
	}

	uni->name = bt_gc_realloc(context, uni->name, uni->name ? strlen(uni->name) + 1 : 0, new_length + 1);

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

int32_t bt_union_get_length(bt_Type* uni)
{
	if (uni->category != BT_TYPE_CATEGORY_UNION) return 0;
	return uni->as.selector.types.length;
}

bt_Type* bt_union_get_variant(bt_Type* uni, uint32_t index)
{
	if (uni->category != BT_TYPE_CATEGORY_UNION) return NULL;
	if (index >= uni->as.selector.types.length) return NULL;
	return uni->as.selector.types.elements[index];
}

BOLT_API int32_t bt_union_has_variant(bt_Type* uni, bt_Type* variant)
{
	if (uni->category != BT_TYPE_CATEGORY_UNION) return -1;
	
	for (uint32_t i = 0; i < uni->as.selector.types.length; ++i) {
		if (uni->as.selector.types.elements[i] == variant) return i;
	}

	return -1;
}

bt_Type* bt_make_enum_type(bt_Context* context, bt_StrSlice name, bt_bool is_sealed)
{
	bt_String* owned_name = bt_make_string_hashed_len(context, name.source, name.length);
	bt_Type* result = bt_make_type(context, BT_STRING_STR(owned_name), bt_type_satisfier_same, BT_TYPE_CATEGORY_ENUM);
	result->as.enum_.name = owned_name;
	result->as.enum_.is_sealed = is_sealed;
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
	if (type && type == type->ctx->types.type) return type;
	if (type && type->category == BT_TYPE_CATEGORY_TYPE) return bt_type_dealias(type->as.type.boxed);
	return type;
}

bt_bool bt_is_alias(bt_Type* type)
{
	return type->satisfier == type_satisfier_alias;
}

bt_bool bt_can_cast(bt_Value value, bt_Type* type)
{
	if (bt_is_type(value, type)) return BT_TRUE;

	if (type->category == BT_TYPE_CATEGORY_ENUM) {
		return BT_IS_NUMBER(value) || BT_IS_ENUM(value);
	}

	if (type == type->ctx->types.number && BT_IS_ENUM(value)) return BT_TRUE;

	return BT_FALSE;
}

bt_Value bt_value_cast(bt_Value value, bt_Type* type)
{
	if (BT_IS_OBJECT(value)) return BT_VALUE_NULL;
	if (type == type->ctx->types.any) return value;
	if (type == type->ctx->types.null) return BT_VALUE_NULL;
	if (type == type->ctx->types.number) {
		if (BT_IS_ENUM(value)) {
			uint32_t val = bt_get_enum_val(value);
			return bt_make_number((bt_number)val);
		}
		if (BT_IS_NUMBER(value)) return value;
		return BT_VALUE_NULL;
	}

	if (type->category == BT_TYPE_CATEGORY_ENUM) {
		uint32_t num_val;
		if (BT_IS_NUMBER(value)) {
			num_val = (uint32_t)bt_get_number(value);
		} else if (BT_IS_ENUM(value)) {
			num_val = bt_get_enum_val(value);			
		}
		
		if (type->as.enum_.is_sealed) {
			if (num_val < 0 || num_val >= type->as.enum_.options->length) {
				return BT_VALUE_NULL;
			}
		}
		
		if (BT_IS_NUMBER(value)) {
			return bt_make_enum_val(num_val);
		}
		if (BT_IS_ENUM(value)) return value;
	}

	if (type->category == BT_TYPE_CATEGORY_UNION) {
		for (uint32_t i = 0; i < type->as.selector.types.length; i++) {
			bt_Type* t = type->as.selector.types.elements[i];
			bt_Value as_cast = bt_value_cast(value, t);
			if (as_cast != BT_VALUE_NULL) return as_cast;
		}
	}
	
	return BT_VALUE_NULL;
}

bt_bool bt_is_type(bt_Value value, bt_Type* type)
{
	type = bt_type_dealias(type);
	
	if (type == type->ctx->types.any) return BT_TRUE;

	if (type->category == BT_TYPE_CATEGORY_UNION) {
		bt_TypeBuffer* types = &type->as.selector.types;

		bt_bool found = BT_FALSE;
		for (uint32_t i = 0; i < types->length; i++) {
			if (bt_is_type(value, types->elements[i])) {
				found = BT_TRUE;
				break;
			}
		}

		return found;
	}
	
	if (type == type->ctx->types.null) return value == BT_VALUE_NULL;
	if (type == type->ctx->types.boolean) return BT_IS_BOOL(value);
	if (type == type->ctx->types.number) return BT_IS_NUMBER(value);
	if (type->category == BT_TYPE_CATEGORY_ENUM) return BT_IS_ENUM(value);
	
	if (!BT_IS_OBJECT(value)) return BT_FALSE;
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
		if (BT_OBJECT_GET_TYPE(as_obj) != BT_OBJECT_TYPE_TABLE) return BT_FALSE;

		bt_Table* as_tbl = (bt_Table*)as_obj;

		if (as_tbl->prototype != type->prototype_values) return BT_FALSE;

		bt_Type* orig_type = type;
		uint32_t num_matched = 0;
		while (type) {
			bt_Table* layout = type->as.table_shape.layout;
			if (layout) {
				for (uint32_t i = 0; i < layout->length; i++) {
					bt_TablePair* pair = BT_TABLE_PAIRS(layout) + i;

					bt_Value val = bt_table_get(as_tbl, pair->key);
					if (val == BT_VALUE_NULL) return BT_FALSE;
					if (!bt_is_type(val, (bt_Type*)BT_AS_OBJECT(pair->value))) return BT_FALSE;

					num_matched++;
				}
			}

			type = type->as.table_shape.parent;
		}

		if (orig_type->as.table_shape.map) {
			for (uint32_t i = 0; i < as_tbl->length; i++) {
				bt_TablePair* pair = BT_TABLE_PAIRS(as_tbl) + i;
				if (!bt_is_type(pair->key, orig_type->as.table_shape.key_type)) return BT_FALSE;
				if (!bt_is_type(pair->value, orig_type->as.table_shape.value_type)) return BT_FALSE;
			}
		}
			
		return num_matched == as_tbl->length || !orig_type->as.table_shape.sealed;
	} break;
	case BT_TYPE_CATEGORY_USERDATA: {
		if (BT_OBJECT_GET_TYPE(as_obj) != BT_OBJECT_TYPE_USERDATA) return BT_FALSE;
		bt_Userdata* data = (bt_Userdata*)as_obj;
		return bt_type_dealias(data->type) == bt_type_dealias(type);
	} break;
	case BT_TYPE_CATEGORY_ARRAY: {
		if (BT_OBJECT_GET_TYPE(as_obj) != BT_OBJECT_TYPE_ARRAY) return BT_FALSE;
		if (type->as.array.inner == type->ctx->types.any) return BT_TRUE;
		
		bt_Array* array = (bt_Array*)as_obj;
		
		for (uint32_t i = 0; i < array->length; i++) {
			bt_Value item = array->items[i];
			if (!bt_is_type(item, type->as.array.inner)) {
				return BT_FALSE;
			}
		}

		return BT_TRUE;
	} break;
	}

	return BT_FALSE;
}

bt_Value bt_transmute_type(bt_Value value, bt_Type* type)
{
	type = bt_type_dealias(type);

	if (type == type->ctx->types.string) {
		return BT_VALUE_OBJECT(bt_to_string(type->ctx, value));
	}

	if (type->category == BT_TYPE_CATEGORY_TABLESHAPE) {
		if (!BT_IS_OBJECT(value)) return BT_VALUE_NULL;

		bt_Object* obj = BT_AS_OBJECT(value);
		if (BT_OBJECT_GET_TYPE(obj) != BT_OBJECT_TYPE_TABLE) {
			return BT_VALUE_NULL;
		}

		bt_Table* src = (bt_Table*)obj;

		if (src->prototype == bt_type_get_proto(type->ctx, type)) {
			return value;
		}

		bt_Table* layout = type->as.table_shape.layout;
		
		bt_Table* dst = bt_make_table(type->ctx, layout ? layout->length : 0);

		if (type->as.table_shape.sealed) {
			for (uint32_t i = 0; i < (layout ? layout->length : 0u); ++i) {
				bt_TablePair* pair = BT_TABLE_PAIRS(layout) + i;

				bt_Value val = bt_table_get(src, pair->key);

				if (val == BT_VALUE_NULL && bt_type_is_optional((bt_Type*)BT_AS_OBJECT(pair->value)) == BT_FALSE) {
					bt_runtime_error(type->ctx->current_thread, "Missing field in table type!", NULL);
				}

				bt_table_set(type->ctx, dst, pair->key, val);
			}
		}
		else {
			for (uint32_t i = 0; i < src->length; i++) {
				bt_TablePair* pair = BT_TABLE_PAIRS(src) + i;
				bt_table_set(type->ctx, dst, pair->key, pair->value);
			}
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
	
	// Unions of size one should be treated like their contained type
	if (a->category != b->category) {
		if (a->category == BT_TYPE_CATEGORY_UNION && a->as.selector.types.length == 1) {
			return bt_type_is_equal(a->as.selector.types.elements[0], b);
		}

		if (b->category == BT_TYPE_CATEGORY_UNION && b->as.selector.types.length == 1) {
			return bt_type_is_equal(a, b->as.selector.types.elements[0]);
		}
		
		return BT_FALSE;
	}

	switch (a->category) {
	case BT_TYPE_CATEGORY_ARRAY: return bt_type_is_equal(a->as.array.inner, b->as.array.inner);
	case BT_TYPE_CATEGORY_TABLESHAPE: 
		if (a->prototype_values) return a->prototype_values == b->prototype_values;
		else if (a->as.table_shape.sealed != b->as.table_shape.sealed) return BT_FALSE;
		else if (a->as.table_shape.parent != b->as.table_shape.parent) return BT_FALSE;
		else if (a->as.table_shape.map != b->as.table_shape.map) return BT_FALSE;
		else if (a->as.table_shape.map) {
			return bt_type_is_equal(a->as.table_shape.key_type, b->as.table_shape.key_type) && bt_type_is_equal(a->as.table_shape.value_type, b->as.table_shape.value_type);
		}else {
			bt_Table* a_layout = a->as.table_shape.layout;
			bt_Table* b_layout = b->as.table_shape.layout;
			if (!a_layout && !b_layout) return BT_TRUE;
			if (a_layout->length != b_layout->length) return BT_FALSE;
		
			for (uint32_t i = 0; i < a_layout->length; i++) {
				bt_TablePair* a_pair = BT_TABLE_PAIRS(a_layout) + i;

				bt_Value b_type = bt_table_get(b_layout, a_pair->key);
				if (!bt_type_is_equal((bt_Type*)BT_AS_OBJECT(a_pair->value), (bt_Type*)BT_AS_OBJECT(b_type))) return BT_FALSE;
			}

			return BT_TRUE;
		}
	case BT_TYPE_CATEGORY_SIGNATURE:
		if (a->is_polymorphic) {
			if (!b->is_polymorphic) return BT_FALSE;
			return a->as.poly_fn.applicator == b->as.poly_fn.applicator;
		}

		if (a->as.fn.is_vararg != b->as.fn.is_vararg) return BT_FALSE;
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
			for (uint32_t j = 0; j < b->as.selector.types.length; ++j) {
				bt_Type* b_current = b->as.selector.types.elements[j];
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
bt_Type* bt_type_bool(bt_Context* context) { return context->types.boolean; }
bt_Type* bt_type_string(bt_Context* context) { return context->types.string; }
bt_Type* bt_type_array(bt_Context* context) { return context->types.array; }
bt_Type* bt_type_table(bt_Context* context) { return context->types.table; }
bt_Type* bt_type_type(bt_Context* context) { return context->types.type; }