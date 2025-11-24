#include "bt_value.h"
#include "bt_object.h"
#include "bt_type.h"
#include "bt_context.h"

#include <math.h>
#include <string.h>

bt_bool bt_value_is_equal(bt_Value a, bt_Value b)
{
	if (a == b) return BT_TRUE;

	if (BT_IS_NUMBER(a)) {
		if (!BT_IS_NUMBER(b)) return BT_FALSE;
		return BT_AS_NUMBER(a) == BT_AS_NUMBER(b);
	}

	if (BT_IS_NUMBER(b)) return BT_FALSE;

	if (BT_IS_OBJECT(a) && BT_IS_OBJECT(b)) {
		bt_Object* obja = BT_AS_OBJECT(a);
		bt_Object* objb = BT_AS_OBJECT(b);

		// Slow vs unslow object reference comparison, missed by fast path above
		if (obja == objb) return BT_TRUE;

		uint8_t type = BT_OBJECT_GET_TYPE(obja);
		if (type == BT_OBJECT_GET_TYPE(objb)) {
			if (type == BT_OBJECT_TYPE_STRING) {
				bt_String* a_str = (bt_String*)obja;
				bt_String* b_str = (bt_String*)objb;
				if (a_str == b_str) return BT_TRUE;
				if (a_str->len != b_str->len) return BT_FALSE;
				if (a_str->interned && b_str->interned) return BT_FALSE;
				if (a_str->hash && b_str->hash) return a_str->hash == b_str->hash;
				return strncmp(BT_STRING_STR(a_str), BT_STRING_STR(b_str), a_str->len) == 0;
			} else if (type == BT_OBJECT_TYPE_TYPE) {
				bt_Type* ta = bt_type_dealias((bt_Type*)obja);
				bt_Type* tb = bt_type_dealias((bt_Type*)objb);
				return bt_type_is_equal(ta, tb);
			}
		}
	}

	return BT_FALSE;
}

bt_Value bt_default_value(bt_Context* ctx, bt_Type* type) {
	if (type == ctx->types.any) return BT_VALUE_NULL;
	if (type == ctx->types.null) return BT_VALUE_NULL;
	if (type == ctx->types.boolean) return BT_VALUE_FALSE;
	if (type == ctx->types.number) return bt_make_number(0);
	if (type == ctx->types.string) return BT_VALUE_OBJECT(bt_make_string_empty(ctx, 0));
	if (type->category == BT_TYPE_CATEGORY_ENUM) return BT_TABLE_PAIRS(type->as.enum_.options)[0].value;
	if (type->category == BT_TYPE_CATEGORY_ARRAY) return BT_VALUE_OBJECT(bt_make_array(ctx, 0));
	if (type->category == BT_TYPE_CATEGORY_UNION) {
		bt_TypeBuffer* types = &type->as.selector.types;
		for (uint32_t idx = 0; idx < types->length; ++idx) {
			bt_Type* elem = types->elements[idx];
			if (elem->category == BT_TYPE_CATEGORY_PRIMITIVE ||
				elem->category == BT_TYPE_CATEGORY_ENUM ||
				elem->category == BT_TYPE_CATEGORY_ARRAY)
				return bt_default_value(ctx, elem);
		}

		return bt_default_value(ctx, types->elements[0]);
	}

	if (type->category == BT_TYPE_CATEGORY_TABLESHAPE) {
		bt_Table* table = bt_make_table_from_proto(ctx, type);
		return BT_VALUE_OBJECT(table);
	}

	bt_runtime_error(ctx->current_thread, "Failed to create default value from complex type", 0);
	return BT_VALUE_NULL;
}

bt_Value bt_make_null() { return BT_VALUE_NULL; }
bt_bool bt_is_null(bt_Value val) { return val == BT_VALUE_NULL; }

bt_Value bt_make_number(bt_number num) { return *((bt_Value*)(bt_number*)&num); }
bt_bool bt_is_number(bt_Value val) { return BT_IS_NUMBER(val); }
bt_number bt_get_number(bt_Value val) { return *((bt_number*)(bt_Value*)&val); }

bt_Value bt_make_bool(bt_bool cond) { return BT_VALUE_BOOL(cond); }
bt_bool bt_is_bool(bt_Value val) { return BT_IS_BOOL(val); }
bt_bool bt_get_bool(bt_Value val) { return val == BT_VALUE_TRUE; }

bt_Value bt_make_enum_val(uint32_t val) { return BT_VALUE_ENUM(val); }
bt_bool bt_is_enum_val(bt_Value val) { return BT_IS_ENUM(val); }
uint32_t bt_get_enum_val(bt_Value val) { return BT_AS_ENUM(val); }

bt_Value bt_value(bt_Object* obj) { return BT_VALUE_OBJECT(obj); }
bt_bool bt_is_object(bt_Value val) { return BT_IS_OBJECT(val); }
bt_Object* bt_object(bt_Value val) { return BT_AS_OBJECT(val); }