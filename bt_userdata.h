#pragma once

#include "bt_type.h"

typedef bt_Value (*bt_UserdataFieldGetter)(bt_Context* ctx, uint8_t* userdata, uint32_t offset);
typedef void     (*bt_UserdataFieldSetter)(bt_Context* ctx, uint8_t* userdata, uint32_t offset, bt_Value value);

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

void bt_userdata_type_field_double(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset); 

void bt_userdata_type_field_float(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset); 

void bt_userdata_type_field_int8(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
void bt_userdata_type_field_int16(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
void bt_userdata_type_field_int32(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
void bt_userdata_type_field_int64(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

void bt_userdata_type_field_uint8(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
void bt_userdata_type_field_uint16(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
void bt_userdata_type_field_uint32(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);
void bt_userdata_type_field_uint64(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset);

void bt_userdata_type_method(bt_Context* ctx, bt_Type* type, const char* name, 
	bt_NativeProc method, bt_Type* ret, bt_Type** args, uint8_t arg_count);