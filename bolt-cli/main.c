
#include <stdio.h>

#include "bolt.h"
#include "boltstd/boltstd.h"

int main(int argc, char** argv) {
	if (argc < 2) {
		printf("USAGE: bolt.exe module_name\n");
		return 1;
	}

	bt_Context* context;
	bt_Handlers handlers = bt_default_handlers();

	bt_open(&context, &handlers);
	boltstd_open_all(context);

	bt_append_module_path(context, "%s");

	bt_Module* mod = bt_find_module(context, BT_VALUE_CSTRING(context, argv[1]));

	if (mod == NULL) {
		printf("ERROR: Failed to import module '%s'!\n", argv[1]);
	}

	bt_close(context);

	return 0;
}