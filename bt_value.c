#include "bt_value.h"
#include "bt_object.h"

#include <math.h>
#include <string.h>

bt_bool bt_value_is_equal(bt_Value a, bt_Value b)
{
	if ((a & BT_TYPE_MASK) != (b & BT_TYPE_MASK)) return BT_FALSE;

	if (BT_IS_NUMBER(a)) {
		return fabs(BT_AS_NUMBER(a) - BT_AS_NUMBER(b)) < BT_EPSILON;
	}

	if (BT_IS_STRING(a)) {
		bt_String* a_str = BT_AS_STRING(a);
		bt_String* b_str = BT_AS_STRING(b);
		if (a_str->hash != 0 && a_str->hash == b_str->hash) return BT_TRUE;
		return strncmp(a_str->str, b_str->str, (size_t)fmax(a_str->len, b_str->len)) == 0;
	}

	return a == b;
}
