#pragma once

#include "bt_buffer.h"
#include "bt_object.h"
#include "bt_userdata.h"

/***
 * Types are an important fundamental object in bolt, as every single binding or field is strongly typed by bolt's parser.
 * Fundamental types are special-cased by the language, and there's not much that be done to extend them at all,
 * but every other type can fully be controlled through the types api.
 *
 * The creation of custom tableshape and userdata types is pretty prevalent throughout the bolt standard library as an example,
 * as well as creating compound types for the sake of function signatures and return types.
 */

typedef struct bt_Type bt_Type;

typedef bt_bool (*bt_TypeSatisfier)(bt_Type* left, bt_Type* right);
typedef bt_Type* (*bt_PolySignature)(bt_Context* ctx, bt_Type** args, uint8_t argc);

/** FUNDAMENTAL TYPES */

/** Returns the primitive `any` type */
BOLT_API bt_Type* bt_type_any(bt_Context* context);
/** Returns the primitive `null` type */
BOLT_API bt_Type* bt_type_null(bt_Context* context);
/** Returns the primitive `number` type*/
BOLT_API bt_Type* bt_type_number(bt_Context* context);
/** Returns the primitive `bool` type */
BOLT_API bt_Type* bt_type_bool(bt_Context* context);
/** Returns the primitive `string` type */
BOLT_API bt_Type* bt_type_string(bt_Context* context);
/** Returns the base `array` type, equal to `[any]` */
BOLT_API bt_Type* bt_type_array(bt_Context* context);
/** Returns the base `table` type, equal to `unsealed {}` */
BOLT_API bt_Type* bt_type_table(bt_Context* context);
/** Returns the primitive `Type` type */
BOLT_API bt_Type* bt_type_type(bt_Context* context);

/** Creates a primitive type, like `number`, `bool` etc */
BOLT_API bt_Type* bt_make_primitive_type(bt_Context* ctx, const char* name, bt_TypeSatisfier satisfier);
/** Creates a boxed alias type - shouldn't be necessary anymore */
BOLT_API bt_Type* bt_make_alias_type(bt_Context* context, const char* name, bt_Type* boxed);
/** Creates the fundamental `Type` type */
BOLT_API bt_Type* bt_make_fundamental_type(bt_Context* context);
/** Creates an opaque userdata type with name `name` */
BOLT_API bt_Type* bt_make_userdata_type(bt_Context* context, const char* name);
/** Creates an array type of `[inner]` */
BOLT_API bt_Type* bt_make_array_type(bt_Context* context, bt_Type* inner);

/** FUNCTION TYPES */

/**
 * Creates a type representing a bolt function signature, in the form `fn (..args): ret`
 * Supplying `NULL` as the return type is valid if the function returns nothing.
 */
BOLT_API bt_Type* bt_make_signature_type(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count);
/** DEPRECATED: The same as bt_make_signature_type, but allows the function to be methodcalled */
BOLT_API bt_Type* bt_make_method_type(bt_Context* context, bt_Type* ret, bt_Type** args, uint8_t arg_count);
/** Converts supplied signature `original` into a vararg function, taking an unspecified number of `varargs_type` arguments */
BOLT_API bt_Type* bt_make_signature_vararg(bt_Context* context, bt_Type* original, bt_Type* varargs_type);
/**
 * Creates a type representing a bolt function signature with polymorphic arguments.
 * `applicator` is called by the bolt parser whenever this function is called, and it's responsible for
 * generating a resulting signature type if possible, otherwise returning NULL.
 */
BOLT_API bt_Type* bt_make_poly_signature_type(bt_Context* context, const char* name, bt_PolySignature applicator);
/** DEPRECATED: The methodcall version of bt_make_poly_signature_type */
BOLT_API bt_Type* bt_make_poly_method_type(bt_Context* context, const char* name, bt_PolySignature applicator);

/** TABLESHAPE TYPES */

/** Creates an empty tableshape type. If `sealed´ is BT_FALSE, assignments to undefined keys is allowed */
BOLT_API bt_Type* bt_make_tableshape_type(bt_Context* context, const char* name, bt_bool sealed);
/**
 * Adds a field to the layout of `tshp` - `key_type` must match the type of `key`
 * Equivalent to appending `{ key: type }` to the tableshape.
 */
BOLT_API void bt_tableshape_add_layout(bt_Context* context, bt_Type* tshp, bt_Type* key_type, bt_Value key, bt_Type* type);
/** Searches for the layout entry with name `key` and returns its' type, or NULL */
BOLT_API bt_Type* bt_tableshape_get_layout(bt_Type* tshp, bt_Value key);
/** Sets `parent` as the parent type for ´tshp´, inhering all fields and protoype entries */
BOLT_API void bt_tableshape_set_parent(bt_Context* context, bt_Type* tshp, bt_Type* parent);
/** Assigns the annotation list `annotations` to the table field `key` in `tshp` */
BOLT_API void bt_tableshape_set_field_annotations(bt_Context* context, bt_Type* tshp, bt_Value key, bt_Annotation* annotations);
/** Gets the annotation list for table field `key` in `tshp`, or NULL if none exist */
BOLT_API bt_Annotation* bt_tableshape_get_field_annotations(bt_Type* tshp, bt_Value key);
/** Creates map type `{ ..key: value }` */
BOLT_API bt_Type* bt_make_map(bt_Context* context, bt_Type* key, bt_Type* value);

/** UNION TYPES */

/** Creates a new empty union type */
BOLT_API bt_Type* bt_make_union(bt_Context* context);
/** Extends `uni` with a new subtype, or allocates a new union if `NULL` is passed */
BOLT_API bt_Type* bt_make_or_extend_union(bt_Context* context, bt_Type* uni, bt_Type* variant);
/** Create a union type of `types` */
BOLT_API bt_Type* bt_make_union_from(bt_Context* context, bt_Type** types, size_t type_count);
/** Adds a new variant to union `uni` */
BOLT_API void bt_union_push_variant(bt_Context* context, bt_Type* uni, bt_Type* variant);
/** Returns the number of variants in union type `uni`, or 0 if it is not a union */
BOLT_API int32_t bt_union_get_length(bt_Type* uni);
/** Returns the variants in union type `uni` at `index`, or NULL */
BOLT_API bt_Type* bt_union_get_variant(bt_Type* uni, uint32_t index);
/** Find `variant` in union type `uni`, returning the index or `-1` on failure */
BOLT_API int32_t bt_union_has_variant(bt_Type* uni, bt_Type* variant);
/** Convenience function to determine whether a union type contains `null` or `any` */
BOLT_API bt_bool bt_type_is_optional(bt_Type* type);
/** Creates a union type of `null` and `to_nullable` if `to_nullable` isn't nullable already */
BOLT_API bt_Type* bt_type_make_nullable(bt_Context* context, bt_Type* to_nullable);
/** Returns a new union type containing all variants of `to_unnull` except `null` */
BOLT_API bt_Type* bt_type_remove_nullable(bt_Context* context, bt_Type* to_unnull);

/** ENUM TYPES */

/** Creates a new enum type with alias `name`. `is_sealed` determines whether numeric values can be cast to/from this type. */
BOLT_API bt_Type* bt_make_enum_type(bt_Context* context, bt_StrSlice name, bt_bool is_sealed);
/** Creates a new enum option inside the type. If `name` is already present, the value is overridden */
BOLT_API void bt_enum_push_option(bt_Context* context, bt_Type* enum_, bt_StrSlice name, bt_Value value);
/** Returns whether the enum contains an option that maps to `value` */
BOLT_API bt_Value bt_enum_contains(bt_Context* context, bt_Type* enum_, bt_Value value);
/** Get the value mapped to the option named `name`, or `null` if it doesn't exist */
BOLT_API bt_Value bt_enum_get(bt_Context* context, bt_Type* enum_, bt_String* name);

/** TYPE PROTOTYPES */

/** Gets the prototype value table associated with `type` */
BOLT_API bt_Table* bt_type_get_proto(bt_Context* context, bt_Type* type);
/** Adds a new field to the prototype table of `type`, with supplied value, name, and type */
BOLT_API void bt_type_add_field(bt_Context* context, bt_Type* type, bt_Type* value_type, bt_Value name, bt_Value value);
/** Sets and overrides an existing prototype field `name` with `value` in `type` */
BOLT_API void bt_type_set_field(bt_Context* context, bt_Type* type, bt_Value name, bt_Value value);
/** Attempts to extract `key` from the type's prototype, writing the result to `value` and returning whether it was present */
BOLT_API bt_bool bt_type_get_field(bt_Context* context, bt_Type* tshp, bt_Value key, bt_Value* value);

/** TYPE UTILS */

/** Extracts the fundamental type from the type alias `type` */
BOLT_API bt_Type* bt_type_dealias(bt_Type* type);
/** Returns whether `type` is an alias or not */
BOLT_API bt_bool bt_is_alias(bt_Type* type);
/** Returns whether `value´ can safely by cast to `type` */
BOLT_API bt_bool bt_can_cast(bt_Value value, bt_Type* type);
/** Performs a value-level cast (NO OBJECT TYPES!) on `value`, returning a new boxed value */
BOLT_API bt_Value bt_value_cast(bt_Value value, bt_Type* type);
/** Returns whether `value` is of type `type` */
BOLT_API bt_bool bt_is_type(bt_Value value, bt_Type* type);
/** Attempt to create a new value of type `type` that is reasonable cast from `value`, returning NULL if not possible */
BOLT_API bt_Value bt_transmute_type(bt_Value value, bt_Type* type);
/** Returns whether types `a` and `b` are functionally equal */
BOLT_API bt_bool bt_type_is_equal(bt_Type* a, bt_Type* b);

static inline bt_bool bt_type_satisfier_any(bt_Type* left, bt_Type* right) { return BT_TRUE; }
static inline bt_bool bt_type_satisfier_same(bt_Type* left, bt_Type* right) { return left == right; }

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
            bt_bool is_sealed;
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