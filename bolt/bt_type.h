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
	BT_TYPE_CATEGORY_ENUM,
} bt_TypeCategory;

typedef bt_Buffer(bt_Type*) bt_TypeBuffer;

typedef struct bt_Type {
	bt_Object obj;

	union {
		struct {
			bt_TypeBuffer types;
		} selector;

		struct {
			bt_Table* tmpl;

			bt_Table* layout;
			bt_Table* key_layout;
			bt_Table* field_annotations;
			bt_Type* parent;
			bt_Type* key_type;
			bt_Type* value_type;
			bt_bool sealed : 1;
			bt_bool final : 1;
			bt_bool map : 1;
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
			bt_Type* boxed;
		} type;

		struct {
			bt_FieldBuffer fields;
			bt_MethodBuffer functions;
			bt_UserdataFinalizer finalizer;
		} userdata;

		struct {
			bt_String* name;
			bt_Table* options;
		} enum_;
	} as;

	bt_Context* ctx;
	char* name;
	bt_TypeSatisfier satisfier;
	
	bt_Type* prototype;
	bt_Table* prototype_types;
	bt_Table* prototype_values;
	bt_Annotation* annotations;
	
	uint8_t category : 5;
	bt_bool is_polymorphic : 1;
} bt_Type;

BOLT_API bt_bool bt_is_optional(bt_Type* type);

static inline bt_bool bt_type_satisfier_any(bt_Type* left, bt_Type* right) { return BT_TRUE; }
static inline bt_bool bt_type_satisfier_same(bt_Type* left, bt_Type* right) { return left == right; }
static inline bt_bool bt_type_satisfier_null(bt_Type* left, bt_Type* right) { return bt_type_satisfier_same(left, right); }

BOLT_API bt_bool bt_type_satisfier_array(bt_Type* left, bt_Type* right);
BOLT_API bt_bool bt_type_satisfier_table(bt_Type* left, bt_Type* right);
BOLT_API bt_bool bt_type_satisfier_union(bt_Type* left, bt_Type* right);

/** Getters for primitive types stored inside the context */
BOLT_API bt_Type* bt_type_any(bt_Context* context);
BOLT_API bt_Type* bt_type_null(bt_Context* context);
BOLT_API bt_Type* bt_type_number(bt_Context* context);
BOLT_API bt_Type* bt_type_boolean(bt_Context* context);
BOLT_API bt_Type* bt_type_string(bt_Context* context);
BOLT_API bt_Type* bt_type_array(bt_Context* context);
BOLT_API bt_Type* bt_type_table(bt_Context* context);
BOLT_API bt_Type* bt_type_type(bt_Context* context);

BOLT_API bt_Type* bt_make_type(bt_Context* context, const char* name, bt_TypeSatisfier satisfier, bt_TypeCategory category);
BOLT_API bt_Type* bt_derive_type(bt_Context* context, bt_Type* original);
BOLT_API bt_Type* bt_make_nullable(bt_Context* context, bt_Type* to_nullable);
BOLT_API bt_Type* bt_remove_nullable(bt_Context* context, bt_Type* to_unnull);
BOLT_API bt_Type* bt_make_signature(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count);
BOLT_API bt_Type* bt_make_method(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count);
BOLT_API bt_Type* bt_make_vararg(bt_Context* context, bt_Type* original, bt_Type* varargs_type);
BOLT_API bt_Type* bt_make_alias(bt_Context* context, const char* name, bt_Type* boxed);
BOLT_API bt_Type* bt_make_fundamental(bt_Context* context);
BOLT_API bt_Type* bt_make_userdata_type(bt_Context* context, const char* name);
BOLT_API bt_Type* bt_make_poly_signature(bt_Context* context, const char* name, bt_PolySignature applicator);
BOLT_API bt_Type* bt_make_poly_method(bt_Context* context, const char* name, bt_PolySignature applicator);

BOLT_API bt_Type* bt_make_tableshape(bt_Context* context, const char* name, bt_bool sealed);
BOLT_API void bt_tableshape_add_layout(bt_Context* context, bt_Type* tshp, bt_Type* key_type, bt_Value key, bt_Type* type);
BOLT_API bt_Type* bt_tableshape_get_layout(bt_Type* tshp, bt_Value key);
BOLT_API void bt_tableshape_set_parent(bt_Context* context, bt_Type* tshp, bt_Type* parent);
BOLT_API void bt_tableshape_set_field_annotations(bt_Context* context, bt_Type* tshp, bt_Value key, bt_Annotation* annotations);
BOLT_API bt_Annotation* bt_tableshape_get_field_annotations(bt_Type* tshp, bt_Value key);

BOLT_API bt_Type* bt_make_map(bt_Context* context, bt_Type* key, bt_Type* value);

BOLT_API bt_Table* bt_type_get_proto(bt_Context* context, bt_Type* tshp);
BOLT_API void bt_type_add_field(bt_Context* context, bt_Type* tshp, bt_Type* type, bt_Value name, bt_Value value);
BOLT_API void bt_type_set_field(bt_Context* context, bt_Type* tshp, bt_Value name, bt_Value value);
/** Attempts to extract `key` from the type's prototype, writing the result to `value` and returning whether it was present */
BOLT_API bt_bool bt_type_get_field(bt_Context* context, bt_Type* tshp, bt_Value key, bt_Value* value);

BOLT_API bt_Type* bt_make_array_type(bt_Context* context, bt_Type* inner);

BOLT_API bt_Type* bt_make_union(bt_Context* context);
BOLT_API bt_Type* bt_make_or_extend_union(bt_Context* context, bt_Type* uni, bt_Type* variant);
BOLT_API void bt_push_union_variant(bt_Context* context, bt_Type* uni, bt_Type* variant);
BOLT_API bt_bool bt_union_has_variant(bt_Type* uni, bt_Type* variant);

BOLT_API bt_Type* bt_make_enum(bt_Context* context, bt_StrSlice name);
BOLT_API void bt_enum_push_option(bt_Context* context, bt_Type* enum_, bt_StrSlice name, bt_Value value);
BOLT_API bt_Value bt_enum_contains(bt_Context* context, bt_Type* enum_, bt_Value value);
BOLT_API bt_Value bt_enum_get(bt_Context* context, bt_Type* enum_, bt_String* name);

BOLT_API bt_Type* bt_type_dealias(bt_Type* type);
BOLT_API bt_bool bt_is_alias(bt_Type* type);
BOLT_API bt_bool bt_is_type(bt_Value value, bt_Type* type);
BOLT_API bt_bool bt_satisfies_type(bt_Value value, bt_Type* type);
BOLT_API bt_Value bt_cast_type(bt_Value value, bt_Type* type);
BOLT_API bt_bool bt_type_is_equal(bt_Type* a, bt_Type* b);