#include "bt_prelude.h"

#include <string.h>

bt_bool bt_strslice_compare(bt_StrSlice a, bt_StrSlice b)
{
	return strncmp(a.source, b.source, a.length > b.length ? a.length : b.length) == 0;
}
