#include "bt_type.h"
#include "bt_userdata.h"
#include "bt_context.h"

#include <assert.h>
#include <memory.h>

static void push_userdata_field(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset,
	bt_Type* field_type, bt_UserdataFieldGetter getter, bt_UserdataFieldSetter setter)
{
	assert(type->category == BT_TYPE_CATEGORY_USERDATA);

	bt_FieldBuffer* fields = &type->as.userdata.fields;

	bt_UserdataField field;
	field.bolt_type = field_type;
	field.name = bt_make_string(ctx, name);
	field.offset = offset;
	field.getter = getter;
	field.setter = setter;

	bt_buffer_push(ctx, fields, field);
}

#define DEFINE_USERDATA_NUMBER_FIELD(fnname, dtype)                                                                \
static bt_Value userdata_get_##fnname(bt_Context* ctx, uint8_t* userdata, uint32_t offset)                         \
{																										           \
	return bt_make_number((bt_number)(*(dtype*)(userdata + offset)));								               \
}																										           \
																										           \
static void userdata_set_##fnname(bt_Context* ctx, uint8_t* userdata, uint32_t offset, bt_Value value)	           \
{																										           \
	assert(BT_IS_NUMBER(value));																		           \
	*(dtype*)(userdata + offset) = (dtype)bt_get_number(value);											           \
}																										           \
																										           \
void bt_userdata_type_field_##fnname(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset)	           \
{						                                                                                           \
	push_userdata_field(ctx, type, name, offset, ctx->types.number, userdata_get_##fnname, userdata_set_##fnname); \
}																										  

DEFINE_USERDATA_NUMBER_FIELD(double, bt_number); 
DEFINE_USERDATA_NUMBER_FIELD(float,  float); 

DEFINE_USERDATA_NUMBER_FIELD(int8,  int8_t);
DEFINE_USERDATA_NUMBER_FIELD(int16, int16_t);
DEFINE_USERDATA_NUMBER_FIELD(int32, int32_t);
DEFINE_USERDATA_NUMBER_FIELD(int64, int64_t);

DEFINE_USERDATA_NUMBER_FIELD(uint8,  uint8_t);
DEFINE_USERDATA_NUMBER_FIELD(uint16, uint16_t);
DEFINE_USERDATA_NUMBER_FIELD(uint32, uint32_t);
DEFINE_USERDATA_NUMBER_FIELD(uint64, uint64_t);

static bt_Value userdata_get_string(bt_Context* ctx, uint8_t* userdata, uint32_t offset)
{
	char* data = *(char**)(userdata + offset);
	uint32_t len = *(uint32_t*)(userdata + offset + sizeof(char*));

	bt_String* as_bolt = bt_make_string_len(ctx, *data ? data : "", *data ? len : 0);

	return BT_VALUE_OBJECT(as_bolt);
}

static void userdata_set_string(bt_Context* ctx, uint8_t* userdata, uint32_t offset, bt_Value value)
{
	assert(BT_IS_OBJECT(value) && BT_OBJECT_GET_TYPE(BT_AS_OBJECT(value)) == BT_OBJECT_TYPE_STRING);

	bt_String* as_str = (bt_String*)BT_AS_OBJECT(value);

	char** data = (char**)(userdata + offset);
	uint32_t* len = (uint32_t*)(userdata + offset + sizeof(char*));

	if (*data) {
		ctx->free(*data);
	}

	*data = ctx->alloc(as_str->len + 1);
	memcpy(*data, BT_STRING_STR(as_str), as_str->len);
	*data[as_str->len] = 0;

	*len = as_str->len;
}

static bt_Value userdata_get_bool(bt_Context* ctx, uint8_t* userdata, uint32_t offset)
{
	bt_bool value = *(bt_bool*)(userdata + offset);
	if (value == BT_TRUE) return BT_VALUE_TRUE;
	return BT_VALUE_FALSE;
}

static void userdata_set_bool(bt_Context* ctx, uint8_t* userdata, uint32_t offset, bt_Value value)
{
	bt_bool* ref = (bt_bool*)(userdata + offset);
	if (value == BT_VALUE_TRUE) *ref = BT_TRUE;
	if (value == BT_VALUE_FALSE) *ref = BT_FALSE;

	assert(!"Impossible value passed to userdata setter!");
}

void bt_userdata_type_field_string(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset)
{
	push_userdata_field(ctx, type, name, offset, ctx->types.string, userdata_get_string, userdata_set_string);
}

void bt_userdata_type_field_bool(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset)
{
	push_userdata_field(ctx, type, name, offset, ctx->types.boolean, userdata_get_bool, userdata_set_bool);
}

void bt_userdata_type_method(bt_Context* ctx, bt_Type* type, const char* name,
	bt_NativeProc method, bt_Type* ret, bt_Type** args, uint8_t arg_count)
{
	assert(type->category == BT_TYPE_CATEGORY_USERDATA);												  
		
	bt_MethodBuffer* methods = &type->as.userdata.functions;														  

	bt_Type* signature = bt_make_signature(ctx, ret, args, arg_count);
	if (arg_count > 0 && args[0] == type) signature->as.fn.is_method = BT_TRUE;

	bt_NativeFn* fn = bt_make_native(ctx, signature, method);

	bt_UserdataMethod me;
	me.name = bt_make_string(ctx, name);
	me.fn = fn;

	bt_buffer_push(ctx, methods, me);
}

void bt_userdata_type_set_finalizer(bt_Type* type, bt_UserdataFinalizer finalizer)
{
	type->as.userdata.finalizer = finalizer;
}
