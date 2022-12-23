#pragma once

#include "bt_buffer.h"
#include "bt_object.h"

typedef struct bt_Type bt_Type;

typedef bt_bool(*bt_TypeSatisfier)(bt_Type* left, bt_Type* right);

typedef enum {
	BT_TYPE_CATEGORY_TYPE,
	BT_TYPE_CATEGORY_PRIMITIVE,
	BT_TYPE_CATEGORY_ARRAY,
	BT_TYPE_CATEGORY_TABLESHAPE,
	BT_TYPE_CATEGORY_SIGNATURE,
	BT_TYPE_CATEGORY_NATIVE_FN,
} bt_TypeCategory;

typedef struct bt_Type {
	bt_Object obj;

	union {
		struct {
			bt_Buffer types;
		} composite, selector;

		struct {
			bt_Table* layout;
			bt_Table* proto;
			bt_bool sealed;
		} table_shape;

		struct {
			bt_Buffer args;
			bt_Type* return_type;
			bt_bool is_vararg;
			bt_Type* varargs_type;
		} fn;

		struct {
			bt_Type* inner;
		} array;

		struct {
			bt_Type* base;
		} nullable;

		struct {
			bt_Type* boxed;
		} type;
	} as;

	bt_Context* ctx;
	char* name;
	bt_TypeSatisfier satisfier;
	uint8_t category : 7;
	bt_bool is_optional : 1;
} bt_Type;

bt_bool bt_type_satisfier_any(bt_Type* left, bt_Type* right);
bt_bool bt_type_satisfier_null(bt_Type* left, bt_Type* right);
bt_bool bt_type_satisfier_same(bt_Type* left, bt_Type* right);
bt_bool bt_type_satisfier_array(bt_Type* left, bt_Type* right);
bt_bool bt_type_satisfier_table(bt_Type* left, bt_Type* right);

bt_Type* bt_make_type(bt_Context* context, const char* name, bt_TypeSatisfier satisfier, bt_TypeCategory category, bt_bool is_optional);
bt_Type* bt_derive_type(bt_Context* context, bt_Type* original);
bt_Type* bt_make_nullable(bt_Context* context, bt_Type* to_nullable);
bt_Type* bt_remove_nullable(bt_Context* context, bt_Type* to_unnull);
bt_Type* bt_make_signature(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count);
bt_Type* bt_make_vararg(bt_Context* context, bt_Type* original, bt_Type* varargs_type);
bt_Type* bt_make_alias(bt_Context* context, const char* name, bt_Type* boxed);
bt_Type* bt_make_fundamental(bt_Context* context);

bt_Type* bt_make_tableshape(bt_Context* context, const char* name, bt_bool sealed);
void bt_tableshape_add_field(bt_Context* context, bt_Type* tshp, bt_Value name, bt_Type* type);
void bt_tableshape_set_proto(bt_Type* tshp, bt_Table* proto);

bt_bool bt_is_type(bt_Value value, bt_Type* type);