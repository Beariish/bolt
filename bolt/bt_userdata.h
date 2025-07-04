#pragma once

#if __cplusplus
extern "C" {
#endif
	
#include "bt_prelude.h"
#include "bt_value.h"

struct bt_Type;
struct bt_String;
struct bt_NativeFn;

/** Function pointer type for retrieving fields from a userdata object, where `offset` is the supplied byte offset into `userdata` when defining the field */
typedef bt_Value (*bt_UserdataFieldGetter)(bt_Context* ctx, uint8_t* userdata, uint32_t offset);
	
/** Function pointer type for setting a field in a userdata object, where `offset` is the supplied byte offset into `userdata` when defining the field.
 * `value` is guaranteed to always match the type supplied when defining the field
 */
typedef void (*bt_UserdataFieldSetter)(bt_Context* ctx, uint8_t* userdata, uint32_t offset, bt_Value value);

/** Internal representation of a user-accessible field in a userdata type */
typedef struct bt_UserdataField {
	bt_Type* bolt_type;
	bt_String* name;
	bt_UserdataFieldGetter getter;
	bt_UserdataFieldSetter setter;
	uint32_t offset;
} bt_UserdataField;

typedef bt_Buffer(bt_UserdataField) bt_FieldBuffer;

/** Pushes a new field to the userdata type `type`
 * `name` is the string matched against when accessing this field from bolt (i.e "field" in T.field)
 * `offset` is measured in bytes from the start of the userdata allocation
 * `field_type` is the bolt type represented in this field, both the getter and setter are expected to take/return this
 * `getter` is called anytime the value stored in this field is accessed, and is responsible for producing a bolt value matching `field_type`
 * `setter` is called whenever this field is written to, and takes a `field_type`
 */
BOLT_API void bt_userdata_type_push_field(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset, bt_Type* field_type, bt_UserdataFieldGetter getter, bt_UserdataFieldSetter setter);

/** Pushes a new field at `offset` with appropriate callbacks for a (4-byte) floating point number */
BOLT_API void bt_userdata_type_field_float(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
/** Pushes a new field at `offset` with appropriate callbacks for a (8-byte) double-length floating point number */
BOLT_API void bt_userdata_type_field_double(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

/** Pushes a new field at `offset` with appropriate callbacks for a (1-byte) signed integer */
BOLT_API void bt_userdata_type_field_int8(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
/** Pushes a new field at `offset` with appropriate callbacks for a (2-byte) signed integer */
BOLT_API void bt_userdata_type_field_int16(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
/** Pushes a new field at `offset` with appropriate callbacks for a (4-byte) signed integer */
BOLT_API void bt_userdata_type_field_int32(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
/** Pushes a new field at `offset` with appropriate callbacks for a (8-byte) signed integer */
BOLT_API void bt_userdata_type_field_int64(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

/** Pushes a new field at `offset` with appropriate callbacks for a (1-byte) unsigned integer */
BOLT_API void bt_userdata_type_field_uint8(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
/** Pushes a new field at `offset` with appropriate callbacks for a (2-byte) unsigned integer */
BOLT_API void bt_userdata_type_field_uint16(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
/** Pushes a new field at `offset` with appropriate callbacks for a (4-byte) unsigned integer */
BOLT_API void bt_userdata_type_field_uint32(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
/** Pushes a new field at `offset` with appropriate callbacks for a (8-byte) unsigned integer */
BOLT_API void bt_userdata_type_field_uint64(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

/** Offset for this is expected to point to a char*, immediately followed by a u32 for length */
BOLT_API void bt_userdata_type_field_string(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

/** Expects the bool at `offset` to be of type `bt_bool` */
BOLT_API void bt_userdata_type_field_bool(bt_Context* ctx, bt_Type* type, const char* naem, uint32_t offset);

/** The finalizer is run whenever the userdata object is being garbage collected, as a means to let the user free any unmanaged resources */
BOLT_API void bt_userdata_type_set_finalizer(bt_Type* type, bt_UserdataFinalizer finalizer);

#if __cplusplus
}
#endif