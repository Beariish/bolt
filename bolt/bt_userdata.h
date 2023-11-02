#pragma once

#include "bt_prelude.h"
#include "bt_value.h"

typedef bt_Value (*bt_UserdataFieldGetter)(bt_Context* ctx, uint8_t* userdata, uint32_t offset);
typedef void     (*bt_UserdataFieldSetter)(bt_Context* ctx, uint8_t* userdata, uint32_t offset, bt_Value value);

typedef struct bt_Type bt_Type;
typedef struct bt_String bt_String;
typedef struct bt_NativeFn bt_NativeFn;

typedef struct bt_UserdataField {
	bt_Type* bolt_type;
	bt_String* name;
	bt_UserdataFieldGetter getter;
	bt_UserdataFieldSetter setter;
	uint32_t offset;
} bt_UserdataField;

typedef struct bt_UserdataMethod {
	bt_String* name;
	bt_NativeFn* fn;
} bt_UserdataMethod;

typedef bt_Buffer(bt_UserdataField) bt_FieldBuffer;
typedef bt_Buffer(bt_UserdataMethod) bt_MethodBuffer;

BOLT_API void bt_userdata_type_field_double(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

BOLT_API void bt_userdata_type_field_float(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

BOLT_API void bt_userdata_type_field_int8(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
BOLT_API void bt_userdata_type_field_int16(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
BOLT_API void bt_userdata_type_field_int32(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
BOLT_API void bt_userdata_type_field_int64(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

BOLT_API void bt_userdata_type_field_uint8(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
BOLT_API void bt_userdata_type_field_uint16(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
BOLT_API void bt_userdata_type_field_uint32(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
BOLT_API void bt_userdata_type_field_uint64(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

/** Offset for this is expected to point to a char*, immediately followed by a u32 for length */
BOLT_API void bt_userdata_type_field_string(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

BOLT_API void bt_userdata_type_method(bt_Context* ctx, bt_Type* type, const char* name,
	bt_NativeProc method, bt_Type* ret, bt_Type** args, uint8_t arg_count);

#define BT_ARG_AS(thread, idx, type) \
	(type*)((bt_Userdata*)BT_AS_OBJECT(bt_arg(thread, idx)))->data