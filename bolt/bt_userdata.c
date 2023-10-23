#include "bt_type.h"
#include "bt_userdata.h"
#include "bt_context.h"

#include <assert.h>

#define DEFINE_USERDATA_NUMBER_FIELD(fnname, dtype)                                                       \
static bt_Value userdata_get_##fnname(bt_Context* ctx, uint8_t* userdata, uint32_t offset)                \
{																										  \
	return bt_make_number((bt_number)(*(dtype*)(userdata + offset)));								      \
}																										  \
																										  \
static void userdata_set_##fnname(bt_Context* ctx, uint8_t* userdata, uint32_t offset, bt_Value value)	  \
{																										  \
	assert(BT_IS_NUMBER(value));																		  \
	*(dtype*)(userdata + offset) = (dtype)bt_get_number(value);											  \
}																										  \
																										  \
void bt_userdata_type_field_##fnname(bt_Context* ctx, bt_Type* type, const char* name, uint32_t offset)	  \
{																										  \
	assert(type->category == BT_TYPE_CATEGORY_USERDATA);												  \
																										  \
	bt_FieldBuffer* fields = &type->as.userdata.fields;													  \
																										  \
	bt_UserdataField field;                                                                               \
	field.bolt_type = ctx->types.number;                                                                  \
	field.name = bt_make_string(ctx, name);																  \
	field.offset = offset;																                  \
	field.getter = userdata_get_##fnname;																  \
	field.setter = userdata_set_##fnname;																  \
																										  \
	bt_buffer_push(ctx, fields, field);																      \
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
