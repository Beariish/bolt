
#include <stdio.h>

#include "bolt.h"
#include "boltstd/boltstd.h"

static void boltstd_open_cli(bt_Context* ctx, int argc, char** argv)
{
	bt_Module* module = bt_make_module(ctx);

	bt_Array* args_array = bt_make_array(ctx, argc - 1);
	for (int arg = 1; arg < argc; ++arg) {
		bt_String* as_owned = bt_make_string(ctx, argv[arg]);
		bt_array_push(ctx, args_array, bt_value((bt_Object*)as_owned));
	}

	bt_module_export(ctx, module, bt_make_array_type(ctx, bt_type_string(ctx)), BT_VALUE_CSTRING(ctx, "args"), bt_value((bt_Object*)args_array));
	
	bt_register_module(ctx, BT_VALUE_CSTRING(ctx, "cli"), module);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		printf("USAGE: bolt.exe module_name\n");
		return 1;
	}

	bt_Context* context;
	bt_Handlers handlers = bt_default_handlers();

	bt_open(&context, &handlers);
	
	boltstd_open_all(context);
	boltstd_open_cli(context, argc, argv);

	bt_append_module_path(context, "%s");

	bt_Module* mod = bt_find_module(context, BT_VALUE_CSTRING(context, argv[1]), BT_FALSE);

	if (mod == NULL) {
		printf("ERROR: Failed to import module '%s'!\n", argv[1]);
	}

	bt_close(context);

	return 0;
}