# Bolt Embedding Guide

### Minimal example
[bolt-cli/main.c](https://github.com/Beariish/bolt/blob/main/bolt-cli/main.c) contains a really consice example of how to embed Bolt into an application, totalling in just about 10 lines of code for the entire VM setup, as well as loading a module. This creates a VM with the entire standard library availible as well, so it should be sufficient for simple use-cases.

### Build and VM configuration
In order to configure the Bolt build itself, the file [bt_config.h](https://github.com/Beariish/bolt/blob/main/bolt/bt_config.h) contains a bunch of defines that alters how Bolt is compiled. These all come with a paragraph or so explaining what they actually do. For the most part, they enable better performance on compilers that support it, though some (like bitmasking together pointers) can make debugging the VM more difficult, which is why they're configurable.

Configuring the VM runtime itself is done prior to the call to `bt_open()` by setting fields in `bt_Handlers`. This structure contains a set of callbacks that the VM invokes on certain events, such as memory allocation, reading module files, and writing to the console. As long as the build-config allows it, `bt_default_handlers()` will fully populate this structure with reasonable defaults. (malloc, fopen, printf)

Some further runtime configuration can be done by probing at the `bt_CompilerOptions` inside the Bolt context. These are the default compiler options used whenever a module is imported (as opposed to manually compiling them, where custom options can be supplied). For the most part, these pertain to runtime performance optimizations, but toggling of debug information can also be done.

The final large piece of runtime configuration are the parameters for Bolt's garbage collector. the `bt_gc_` family of functions expose them, from things like when to run future cycles, the minimum allowed heap size, and the max number of intermediate (grey) objects during marking. 

### Api overview
Bolt adheres to a few standards to hopefully make exploring and using the API as simple as possible.
* All Bolt names are prefixed with `bt_`, followed by lower_snake_case for functions, and PascalCase for types.
    * `bt_make_number()`, `bt_push()`, `bt_module_export()` ...
    * `bt_Context`, `bt_NativeFn`, `bt_Thread` ...
* The Bolt API organized such that groups of functions that operate on a specific type are named appropriately.
    * All objects are created through a `bt_make_` function.
        * `bt_make_string()`, `bt_make_thread()` ...
    * Operations on objects follow the `bt_objectype_do_thing` convention.
        * `bt_table_set()`, `bt_table_get()` ...
    * The main exception to this are functions that operate on the Bolt context itself, as this is seen as "global" scope.
* All functions that may allocate (which is most!) take the context as their first parameter.

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
Before we can invoke our native code from Bolt, we need to understand how the VM interacts with our native environment. 
There is exactly one signature of function that can be bound to and invoked in Bolt, and that is:
```c
typedef void (*bt_NativeProc)(bt_Context*, bt_Thread*); 
``` 

The thread object contains all the information we need to process the current call, and the context is used to allocation, gc-access, and forwarding arguments.

So, take a simple function like:
```c
static void my_add_numbers(bt_Context* ctx, bt_Thread* thr)
{
    bt_number a = bt_get_number(bt_arg(thr, 0));
    bt_number b = bt_get_number(bt_arg(thr, 1));
    bt_return(thr, bt_make_number(a + b));
}
```
It all looks pretty straight forward - get arguments from the thread's stack, unbox the values into numbers, and then return a new boxed value. What's quite notably missing here if you've spent time integrating other embedded languages, though, is that there's no checking going on here. We can assume both arguments are present, we can assume they're both numbers. This isn't just for the sake of brevity here, but is part of the beauty of Bolt's type system. All this information is encoded with the export binding itself, meaning the compiler has already done this work for us, and we don't need to waste precious cycles **every time** our function is called to make sure the user knows what they're doing.

This adds up.

So, how do we actually bind this to Bolt? There are a few steps involved.

First, *all* bolt exports **must** live inside a module, so let's make a new one:
```c
bt_Module* my_module = bt_make_user_module(ctx);
```

Then, we need to wrap our function in an object Bolt can reason about:
```c
bt_Type* add_args[] = { bt_type_number(ctx), bt_type_number(ctx) }; // The types of our arguments
// Make a type that represents our function signature: fn(number, number): number
bt_Type* add_signature = bt_make_signature(ctx, bt_type_number(ctx), add_args, 2); // return type, argument types, arity
bt_NativeFn* add_reference = bt_make_native(ctx, add_signature, my_add_numbers);
```

Next, we need to export this to the module we created:
```c
bt_module_export(ctx, my_module, add_signature, BT_VALUE_CSTRING(ctx, "add"), bt_make_object(add_reference));
```

And then, finally, we need to register the new module:
```c
bt_register_module(ctx, BT_VALUE_CSTRING(ctx, "my_module"), my_module);
```

And done! We can now import and run functions from this module in Bolt code!

```rust
import print from core
import add from my_module

print(add(10, 20)) // 30!
```

Because this is a very common thing to do when embedding Bolt, a convenience function exists to combine most of these steps into one:
```c
bt_Type* add_args[] = { bt_type_number(ctx), bt_type_number(ctx) }; // The types of our arguments
bt_module_export_native(ctx, my_module, "add", my_add_number, bt_type_number(ctx), add_args, 2);
```

For more examples of how to embed more complex functions, or encode more intricate types when doing so, I highly recommend looking at the standard library modules (`boltstd/..`) since they contain literally nothing but functions exposed to the bolt runtime.