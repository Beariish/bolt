#include "boltstd.h"

#include "boltstd_arrays.h"
#include "boltstd_core.h"
#include "boltstd_math.h"
#include "boltstd_meta.h"
#include "boltstd_strings.h"
#include "boltstd_io.h"
#include "boltstd_tables.h"

void boltstd_open_all(bt_Context* context)
{
	boltstd_open_core(context);
	boltstd_open_math(context);
	boltstd_open_meta(context);
	boltstd_open_arrays(context);
	boltstd_open_tables(context);
	boltstd_open_strings(context);
	boltstd_open_io(context);
}
