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

				if (ta->category != tb->category) return BT_FALSE;
				if (ta->satisfier != tb->satisfier) return BT_FALSE;

				if (ta->category == BT_TYPE_CATEGORY_UNION) {
					for (uint32_t i = 0; i < ta->as.selector.types.length; i++) {
						bt_Type* inner_a = ta->as.selector.types.elements[i];

						bt_bool found = BT_FALSE;
						for (uint32_t j = 0; j < tb->as.selector.types.length; j++) {
							bt_Type* inner_b = tb->as.selector.types.elements[j];

							found |= bt_value_is_equal(BT_VALUE_OBJECT(inner_a), BT_VALUE_OBJECT(inner_b));
						}

						if (!found) return BT_FALSE;
					}
				}

				return strcmp(ta->name, tb->name) == 0;
			} else if (BT_OBJECT_GET_TYPE(obja) == BT_OBJECT_TYPE_STRING) {
				bt_String* a_str = obja;
				bt_String* b_str = objb;
				if (a_str->hash && b_str->hash) return a_str->hash == b_str->hash;
				return strncmp(a_str->str, b_str->str, (size_t)fmax(a_str->len, b_str->len)) == 0;
			}
		}
	}

	return BT_FALSE;
}