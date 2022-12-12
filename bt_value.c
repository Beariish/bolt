#include "bt_value.h"
#include "bt_object.h"
#include "bt_type.h"

#include <math.h>
#include <string.h>

bt_bool bt_value_is_equal(bt_Value a, bt_Value b)
{
	if (BT_IS_NUMBER(a)) {
		return fabs(BT_AS_NUMBER(a) - BT_AS_NUMBER(b)) < BT_EPSILON;
	}

	if (BT_TYPEOF(a) != BT_TYPEOF(b)) return BT_FALSE;

	if (BT_IS_STRING(a)) {
		bt_String* a_str = BT_AS_STRING(a);
		bt_String* b_str = BT_AS_STRING(b);
		if (a_str->hash != 0 && a_str->hash == b_str->hash) return BT_TRUE;
		return strncmp(a_str->str, b_str->str, (size_t)fmax(a_str->len, b_str->len)) == 0;
	}

	if (BT_IS_OBJECT(a)) {
		bt_Object* obj = BT_AS_OBJECT(a);
		if (obj->type == BT_OBJECT_TYPE_TYPE) {
			bt_Type* ta = obj;
			bt_Type* tb = BT_AS_OBJECT(b);

			if (ta->category != tb->category) return BT_FALSE;
			if (ta->is_optional != tb->is_optional) return BT_FALSE;
			if (ta->satisfier != tb->satisfier) return BT_FALSE;
			// TODO: Can we guarentee this is sane?
			return strcmp(ta->name, tb->name) == 0;
		}
	}

	return a == b;
}