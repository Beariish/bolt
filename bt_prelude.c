#include "bt_prelude.h"

#include <string.h>

bt_bool bt_strslice_compare(bt_StrSlice a, bt_StrSlice b)
{
	if (a.length != b.length) return BT_FALSE;
	return strncmp(a.source, b.source, a.length) == 0;
}
