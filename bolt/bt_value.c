#include "bt_value.h"
#include "bt_object.h"
#include "bt_type.h"

#include <math.h>
#include <string.h>

bt_bool bt_value_is_equal(bt_Value a, bt_Value b)
{
	if (BT_IS_NUMBER(a)) {
		if (!BT_IS_NUMBER(b)) return BT_FALSE;
		return fabs(BT_AS_NUMBER(a) - BT_AS_NUMBER(b)) < BT_EPSILON;
	}

	if (BT_IS_NUMBER(b)) return BT_FALSE;

	if (a == b) return BT_TRUE;

	if (BT_IS_OBJECT_FAST(a) && BT_IS_OBJECT_FAST(b)) {
		bt_Object* obja = BT_AS_OBJECT(a);
		bt_Object* objb = BT_AS_OBJECT(b);

		if (BT_OBJECT_GET_TYPE(obja) == BT_OBJECT_GET_TYPE(objb)) {
			if (BT_OBJECT_GET_TYPE(obja) == BT_OBJECT_TYPE_TYPE) {
				bt_Type* ta = bt_type_dealias(obja);
				bt_Type* tb = bt_type_dealias(objb);
				return ta == tb;
			} else if (BT_OBJECT_GET_TYPE(obja) == BT_OBJECT_TYPE_STRING) {
				bt_String* a_str = obja;
				bt_String* b_str = objb;
				if (a_str->hash && b_str->hash) return a_str->hash == b_str->hash;
				return strncmp(BT_STRING_STR(a_str), BT_STRING_STR(b_str), (size_t)fmax(a_str->len, b_str->len)) == 0;
			}
		}
	}

	return BT_FALSE;
}

#ifdef BOLT_SHARED_LIBRARY
bt_Value bt_make_null() { return BT_VALUE_NULL; }
uint8_t bt_is_null(bt_Value val) { return val == BT_VALUE_NULL; }

bt_Value bt_make_number(bt_number num) { return *((bt_Value*)(bt_number*)&num); }
uint8_t bt_is_number(bt_Value val) { return BT_IS_NUMBER(val); }
bt_number bt_get_number(bt_Value val) { return *((bt_number*)(bt_Value*)&val); }

bt_Value bt_make_bool(uint8_t cond) { return BT_VALUE_BOOL(cond); }
uint8_t bt_is_bool(bt_Value val) { return BT_IS_BOOL(val); }
uint8_t bt_get_bool(bt_Value val) { return val == BT_VALUE_TRUE; }

bt_Value bt_make_enum_val(uint32_t val) { return BT_VALUE_ENUM(val); }
uint8_t bt_is_enum_val(bt_Value val) { return BT_IS_ENUM(val); }
uint32_t bt_get_enum_val(bt_Value val) { return BT_AS_ENUM(val); }

bt_Value bt_make_object(bt_Object* obj) { return BT_VALUE_OBJECT(obj); }
uint8_t bt_is_object(bt_Value val) { return BT_IS_OBJECT(val); }
bt_Object* bt_get_object(bt_Value val) { return BT_AS_OBJECT(val); }
#endif