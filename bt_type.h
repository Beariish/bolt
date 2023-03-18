#pragma once

#include "bt_buffer.h"
#include "bt_object.h"
#include "bt_userdata.h"

typedef struct bt_Type bt_Type;

typedef bt_bool(*bt_TypeSatisfier)(bt_Type* left, bt_Type* right);
typedef bt_Type* (*bt_PolySignature)(bt_Context* ctx, bt_Type** args, uint8_t argc);

typedef enum {
	BT_TYPE_CATEGORY_TYPE,
	BT_TYPE_CATEGORY_PRIMITIVE,
	BT_TYPE_CATEGORY_ARRAY,
	BT_TYPE_CATEGORY_TABLESHAPE,
	BT_TYPE_CATEGORY_SIGNATURE,
	BT_TYPE_CATEGORY_NATIVE_FN,
	BT_TYPE_CATEGORY_USERDATA,
	BT_TYPE_CATEGORY_UNION,
} bt_TypeCategory;

typedef bt_Buffer(bt_Type*) bt_TypeBuffer;

typedef struct bt_Type {
	bt_Object obj;

	union {
		struct {
			bt_TypeBuffer types;
		} selector;

		struct {
			bt_Table* layout;
			bt_Type* parent;
			bt_bool sealed : 1;
			bt_bool final : 1;
		} table_shape;

		struct {
			bt_TypeBuffer args;
			bt_Type* return_type;
			bt_Type* varargs_type;
			bt_bool is_vararg : 1;
			bt_bool is_method : 1;
		} fn;

		struct {
			bt_PolySignature applicator;
		} poly_fn;

		struct {
			bt_Type* inner;
		} array;

		struct {
			bt_Type* base;
		} nullable;

		struct {
			bt_Type* boxed;
		} type;

		struct {
			bt_FieldBuffer fields;
			bt_MethodBuffer functions;
		} userdata;
	} as;

	bt_Context* ctx;
	char* name;
	bt_TypeSatisfier satisfier;
	
	bt_Type* prototype;
	bt_Table* prototype_types;
	bt_Table* prototype_values;

	uint8_t category : 5;
	bt_bool is_compiled : 1;
	bt_bool is_optional : 1;
	bt_bool is_polymorphic : 1;
} bt_Type;

static BT_FORCE_INLINE bt_bool bt_type_satisfier_any(bt_Type* left, bt_Type* right) { return BT_TRUE; }
static BT_FORCE_INLINE bt_bool bt_type_satisfier_same(bt_Type* left, bt_Type* right) { return left == right; }
static BT_FORCE_INLINE bt_bool bt_type_satisfier_null(bt_Type* left, bt_Type* right) { return bt_type_satisfier_same(left, right) | left->is_optional; }

bt_bool bt_type_satisfier_array(bt_Type* left, bt_Type* right);
bt_bool bt_type_satisfier_table(bt_Type* left, bt_Type* right);
bt_bool bt_type_satisfier_union(bt_Type* left, bt_Type* right);

bt_Type* bt_make_type(bt_Context* context, const char* name, bt_TypeSatisfier satisfier, bt_TypeCategory category, bt_bool is_optional);
bt_Type* bt_derive_type(bt_Context* context, bt_Type* original);
bt_Type* bt_make_nullable(bt_Context* context, bt_Type* to_nullable);
bt_Type* bt_remove_nullable(bt_Context* context, bt_Type* to_unnull);
bt_Type* bt_make_signature(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count);
bt_Type* bt_make_method(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count);
bt_Type* bt_make_vararg(bt_Context* context, bt_Type* original, bt_Type* varargs_type);
bt_Type* bt_make_alias(bt_Context* context, const char* name, bt_Type* boxed);
bt_Type* bt_make_fundamental(bt_Context* context);
bt_Type* bt_make_userdata_type(bt_Context* context, const char* name);
bt_Type* bt_make_poly_signature(bt_Context* context, const char* name, bt_PolySignature applicator);

bt_Type* bt_make_tableshape(bt_Context* context, const char* name, bt_bool sealed);
void bt_tableshape_add_layout(bt_Context* context, bt_Type* tshp, bt_Value name, bt_Type* type);
void bt_tableshape_set_parent(bt_Context* context, bt_Type* tshp, bt_Type* parent);

bt_Table* bt_type_get_proto(bt_Context* context, bt_Type* tshp);
void bt_type_add_field(bt_Context* context, bt_Type* tshp, bt_Value name, bt_Value value, bt_Type* type);
void bt_type_set_field(bt_Context* context, bt_Type* tshp, bt_Value name, bt_Value value);

bt_Type* bt_make_array_type(bt_Context* context, bt_Type* inner);

bt_Type* bt_make_union(bt_Context* context);
void bt_push_union_variant(bt_Context* context, bt_Type* uni, bt_Type* variant);

bt_bool bt_is_type(bt_Value value, bt_Type* type);
bt_bool bt_satisfies_type(bt_Value value, bt_Type* type);
bt_Value bt_cast_type(bt_Value value, bt_Type* type);