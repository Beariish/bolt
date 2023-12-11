# Bolt Embedding Guide

### Minimal example
[bolt-cli/main.c](https://github.com/Beariish/bolt/blob/main/bolt-cli/main.c) contains a really consice example of how to embed Bolt into an application, totalling in just about 10 lines of code for the entire VM setup, as well as loading a module. This creates a VM with the entire standard library availible as well, so it should be sufficient for simple use-cases.

### Build and VM configuration
In order to configure the Bolt build itself, the file [bt_config.h](https://github.com/Beariish/bolt/blob/main/bolt/bt_config.h) contains a bunch of defines that alters how Bolt is compiled. These all come with a paragraph or so explaining what they actually do. For the most part, they enable better performance on compilers that support it, though some (like bitmasking together pointers) can make debugging the VM more difficult, which is why they're configurable.

Configuring the VM runtime itself is done prior to the call to `bt_open()` by setting fields in `bt_Handlers`. This structure contains a set of callbacks that the VM invokes on certain events, such as memory allocation, reading module files, and writing to the console. As long as the build-config allows it, `bt_default_handlers()` will fully populate this structure with reasonable defaults. (malloc, fopen, printf)

Some further runtime configuration can be done by probing at the `bt_CompilerOptions` inside the Bolt context. These are the default compiler options used whenever a module is imported (as opposed to manually compiling them, where custom options can be supplied). For the most part, these pertain to runtime performance optimizations, but toggling of debug information can also be done.

The final large piece of runtime configuration are the parameters for Bolt's garbage collector. the `bt_gc_` family of functions expose them, from things like when to run future cycles, the minimum allowed heap size, and the max number of intermediate (grey) objects during marking. 

### Api overview

### Invoking Bolt from C
In order to start Bolt invocation from C, the first step is to acquire a handle to a Bolt function. The main ways to do this is either to have the user `export` the function with a predetermined name, which can be retrieved with `bt_module_get_export()`, or to have them supply it as an argument to a natively-registered function. While module exports are guarenteed to live until the module registry is purged, remember to `bt_add_ref()` the function handle to prevent it from getting collected while stored natively.

Next, how you invoke the function is slightly different depending on whether you're at the "root" of the Bolt callstack. If you're starting a new thread, the `bt_execute_` family of functions is the correct choice. Otherwise, `bt_push()` and `bt_call()` are your friends. Here are some examples:

Given a bolt file that looks like this:
```rust
import core

export fn call_me {
    core.print("I have been called!")
}
```

```c
bt_Module* mod = bt_find_module(ctx, BT_VALUE_CSTRING(ctx, "my_module"));
bt_Value callback = bt_module_get_export(ctx, BT_VALUE_CSTRING(ctx, "call_me"));

if (callback == BT_VALUE_NULL) return; // TODO: Do more typechecking here, with bt_module_get_export_type()

bt_bool success = bt_execute(ctx, callback); // This will allocate a temporary thread to run this in, which can be costly
```

If you intend on calling many root-level functions like this, it can be advantageous to allocate the `bt_Thread` beforehand:

```c
bt_Thread* thr = bt_make_thread(ctx);
bt_bool success = bt_execute_on_thread(ctx, thr, callback);

// later...
bt_destroy_thread(ctx, thr);
```

Now, these obviously don't cover passing arguments. So, imagine we instead have this bolt file:

```rust
import core

export fn call_me(iters: number, message: string) {
    core.print("I have been called, I will now repeat..")

    for i in iters {
        core.print(message)
    }
}
```
And after getting the function handle the exact same way, we can now invoke it like this:

```c
bt_Thread* thr = bt_make_thread(ctx);

bt_Value args[] = { 10, BT_VALUE_CSTRING("HELLO!!!") };
bt_bool success = bt_execute_with_args(ctx, thr, callback, args, 2);

// later...
bt_destroy_thread(ctx, thr);
```

If we're instad in an intermediate call (ie., from C to Bolt, back to C, and then wish to enter Bolt again), we instead manipulate the VM stack directly:

```c
static bt_Value callback;

static void iwascalledfrombolt(bt_Context* ctx, bt_Thread* thr)
{
    bt_push(ctx, callback);                           // push callable
    bt_push(ctx, BT_VALUE_NUMBER(10));                // push args
    bt_push(ctx, BT_VALUE_CSTRING(ctx, "hello :)"));
    bt_call(ctx, 2);                                  // call with argc
}
```

If we have a Bolt function that returns a value:
```rust
export fn call_me { return 10 }
```

The `bt_get_returned()` function will get us the last value returned on that thread:

```c
bt_Thread* thr = bt_make_thread(ctx);

bt_bool success = bt_execute_on_thread(ctx, thr, callback);
bt_Value val = bt_get_returned(thr);
```

### Binding C to Bolt
