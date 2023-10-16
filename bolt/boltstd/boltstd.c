#include "boltstd.h"

#include "boltstd_arrays.h"
#include "boltstd_core.h"
#include "boltstd_math.h"
#include "boltstd_meta.h"
#include "boltstd_strings.h"

void boltstd_open_all(bt_Context* context)
{
	boltstd_open_arrays(context);
	boltstd_open_core(context);
	boltstd_open_math(context);
	boltstd_open_meta(context);
	boltstd_open_strings(context);
}
