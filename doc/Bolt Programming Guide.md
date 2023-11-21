## 1. Language introduction
Bolt is a hybrid-typed, high-level scripting language that functions a lot like other embedded languages. It sports a similar execution model to Lua in particular, which was a large inspiration. One of Bolt's main differentiators, though, is it's use of a type system in dynamic code, providing a few key benefits.
* Code is more maintainable, especially at scale. Wrangling with function definitions in large Lua codebases can be very difficult.
* Errors are caught at compile-time, rather than runtime. Even with embedded languages, that don't necessarily compile with the host application, it's a huge benefit to be able to display errors to the user upfront, rather than whenever offending code gets executed.
* It's great for performance. Bolt is fast. Really, really fast. And a large part of what makes it so fast is allowing the compiler to make optimizations based on the types provided. The Bolt interpreter features specialized instructions that bypass any dynamic type checking normally found in VM's of it's kind, as well as accelerated table/object/array lookups for when the shape is known at compile-time.

A simple bolt program may look something like this:
```rust
import print from core

fn speak(animal: string) {
	if animal == "cat" { return "meow" }
	else if animal == "dog" { return "woof" }
	else if animal == "mouse" { return "squeak" }
	
	return "nothing!"
}

let const animals = [ "cat", "dog", "mouse", "monkey" ]
for const animal in animals.each() {
	print(animal, "says", speak(animal))
}
```

Note that despite being completely statically typed, the only type annotation we actually provided was for the argument `animal: string` inside the function `speak`. Bolt infers types anywhere it can. The full signature of `speak` becomes `fn(string): string` after parsing, and the type of `animals` is deduced to be `[string]`. 

Whenever the compiler isn't sure, or there's no concrete type to deduce, an error is raised asking for more annotations.

## 2. Literals
Literals represent the simplest possible tokens in Bolt, constant values (or compound expressions that produce values) that get evaluated at compile-time and interned into the module binary.

There are 6 types of literals in bolt: `number`, `string`, `boolean`, `array`, `table`, and `null`

#### 1. Numbers
Numbers are denoted in standard notation only, with an optional decimal part.
These are some valid number literals:

```rust
let a = 10
let b = 13.37
let c = 0.29
let d = 20.
```

Note that leading periods for decimal numbers are not allowed.

#### 2. Strings
Strings are any sequence of characters denoted with double quotation marks `""`. They can span across newlines, contain whitespace, and in general preserve the format they were described with.

Literal strings are also hashed at compile-time to speed up comparisons, making them cheap in comparison to strings generated at runtime. 

```rust
let a = "This is a valid bolt string!"
let b = "So is                this!"
let c = "And
this"
```

#### 3. Booleans
Booleans can simply inhabit one of two possible values: `true` or `false`

#### 4. Arrays
Arrays in bolt are dynamically sized, but contain elements of only one type. Array types are declared using the `[T]` syntax, where `T` represents the element type. `[]` is also a shorthand for `[any]`

Array literals are deduced to be their narrowest type during parsing, meaning that:
* `[1, 2, 3, 4]` will produce `[number]`
* `[true, "hello", false]` will produce `[boolean | string]`
* `[10 as any]` will produce `[any]`

A valid array binding may look like:
```rust
let const names: [string] = ["Mike", "Jacob", "Hubert"]
```

Optionally, array literals can be explicitly typed. This is desirable any time an array doesn't contain one of the union variants from the get-go, or if you want to pass an empty array to a function.

```rust
let const names = ["Dave", "Daniel" : string] // does nothing here
let const addresses = ["21st ave", "412 drive" : string?] // allow for null
let const ages = [:number] // empty array of numbers
```
#### 5. Tables
Tables are hashmaps under the hood, mapping a key to a value. Any valid bolt value can be a key, as well as a value, and all indexing operations performed on a table return an `any`.

An example table may look like:
```rust
let cart = {
	bananas: 10,
	apples: 20,
	at_checkout: false
}
```

Getting and setting values can be done with the square bracket operator:
```rust
print(cart["bananas"]) // 10
cart["bananas"] *= 2
print(cart["bananas"]) // 20
```

Or with the dot shorthand, for string keys:
```rust
print(cart.apples) // 20
cart.at_checkout = true
```

What really makes tables interesting and powerful in bolt are table-shape types, which are explained much further in detail later in the guide.

#### 6. Null
`null` is both the simplest type and value in Bolt. The value `null` is the only assignable value of the type `null`. This makes it a little useless on it's own, but powerful when combined with optional types or unions.

```rust
let name: string? = null

if name is null {
	name = ask_for_name()
}

print("hello", name!)
```

Note that `is` and `==` function the same way for `null`, as it is both a type and the only value of said type.

```rust
let config = maybe_get_config()
if config is null or config == null or config? {
	print("These conditions are the same!")
}
```


## 3. `let` and variable declarations
In bolt, the `let` keyword creates a new variable binding within the local scope. Bindings can be shadowed by deeper scopes, but redefinition within the same scope is not allowed.

```rust
let x = 10

if true {
	let x = 20 // allowed!
}

let x = 5 // compiler error!
```

Any block surrounded with braces is considered it's own scope. Function-level scopes represent a boundary where lifting the binding into an upvalue is required.

```rust
let x = 10
fn some_function {
	print(x) // x is captured as an upvalue!
}
```

By default, the type of the binding is inferred from the assignment, but it's also possible to explicitly denote the type.

```rust
let x: number = 10
let y: string = 10 // parser error, mismatched types!
let z: any = 10 // explicitly create dynamically typed binding
```

#### `const`
Specifying `const` after `let` creates a locally immutable binding, prohibiting any mutation of the value within the current scope.
```rust
let const x = 10
x += 10 // compiler error, x is immutable
```

For more complex types, it will even prohibit internal mutability.

```rust
let const guy = {
	age: 25,
	name: "Ronald"
}

guy.age = 20 // compiler error, guy is immutable
```

Note that for reference types, const-ness isn't carried through function arguments (yet!)

```rust
let const guy = {
	age: 25,
	name: "Ronald"
}

fn guy_ager(guy: {}) {
	guy.age = 90
}

guy_ager(guy)
print(guy.age) // 90
```

## 4. Expressions and operators
#### Expressions
An expression in Bolt is anything that is _not_ a statement and produces zero or more values. Simple expressions include value literals (`true`, `10`, `"hello!"`), identifiers (`foo`, `bar`, `bob`), operators (`10 + 10`), and function calls (`add(10, 10)`). All expressions in Bolt are typed.

#### Arithmetic
Bolt supports the standard set of mathematical operators - `+ - * /` and `+= -= *= /=`. These are all type-checked at compile-time, and only pass the check if the types on either side are the same (or if lhs has a metamethod that can take rhs as a parameter, more on that later!)

That means there's no type punning in bolt, and things like `"hello" + 10` that you might come to expect from other dynamic languages is instead an error.

#### Logic
Bolt has a rich set of logical operators as well. The normal `== != and or not > >= < <=` that you may expect work like usual, though there is no short-circuiting yet, so all branches will be evaluated.

All logical operators in Bolt produce boolean values as well, instead of one of their operands.

In addition to those above, Bolt has a few bespoke operators unique to the language.
The operators `?`, `is`, and `satisfies` represent logical operations that reason about the type of a binding - they'll be explained further in the type system portion of the guide.

There are a few other operators pertaining to types (`=>`, `as`, `??`, `!`, `.`, `&`) but these are all related to actual data manipulation and will be explained alongside the rest.

#### Precedence
Operator precedence in Bolt is quite normal, with expressions wrapped in parentheses being evaluated before all else. Here's a table of current precedence:
```rust
&
.         ()
[]        =>        *         /
?         +         -
is        as        satisfies not       +(unary)  -(unary)
??
<         <=        >         >=        !
==        !=
and       or 
+=        -=        *=        /=
=
```

## 5. Functions
Functions are defined with the `fn` keyword, followed by a list of parameters, a return type, and then the body of the function. They are first-class expressions, meaning they can be assigned and passed just like any other value.

```rust
let const adder = fn(x: number, y: number): number {
	return x + y
}
```

Using `fn` at the statement level also exists as shorthand. This means the following is equivalent:

```rust
fn adder(x: number, y: number): number {
	return x + y
}
```

Functions will infer their return type whenever possible, meaning you can often leave it off of the definition. This does _not_ mean that the return value is dynamic, as functions must always return *only one* type. Leaving it off is also how you specify a function that has no return value.

```rust
// this is the same!
fn adder(x: number, y: number) {
	return x + y
}

let x = adder(10, 20) // x: number

// this returns nothing!
fn printer() {
	print("hello!")
}
```

Most syntax in Bolt becomes optional if it's purely structural, meaning an empty parameter list can be left off as well.

```rust
fn printer { 
	print("hello") 
}
```

Parameters with no type specifier are implied to be `any`. 

```rust
fn inspector(x) {
	print("The type of", x, "is", typeof(x))
}

inspector(10) // The type of 10 is Type(any)
inspector(true) // The type of true is Type(any)
```

## 6. Type system
The type system is one of Bolt's main appeals, allowing for larger, more scalable code that runs faster, even in a hosted, dynamic environment. Leveraging it's position as a dynamic language, though, Bolt enables a lot of interesting use cases for types.

#### 1. Type aliasing
The fundamental feature of Bolt's type system is the ability to declare and create new types. The simplest mechanism by which to do that is with the keyword `type`.

```rust
type Balance = number
type Name = string

fn announce(name: Name, balance: Balance) {
	print(name, "has", balance, "dollars!")
}
```

It functions very similarly to `let`, except that type expressions are evaluated at parse-time, and therefor are always available in scope before the code is executed. The example above is a little contrived, though, so let's look at how you can use aliases for more complex types.

#### 2. Function types
Function types, or signatures, look very similar to definitions, sans the argument names. By using function types as aliases, we can easily define type-safe callbacks for example.

```rust
type NumberOperator = fn(number, number): number

fn apply(x: number, y: number, op: NumberOperator) {
	return op(x, y)
}

fn add(x: number, y: number) { return x + y }

print(apply(100, 100, add)) // 200
```

Just like with bodies, function type components are optional if purely there for syntax, meaning the simplest function type is `fn`:

```rust
fn do_thing(on_complete: fn) {
	do_expensive_work()
	on_complete()
}

do_thing(fn {
	print("thing is complete!")
})
```

#### 3. Union types
Union (or sum, if you prefer) types in Bolt allow for the ability to express a binding that can be one of a number of distinct types. This is useful for function overloading, generic programming, optional parameters, error reporting, and so on!

Union types are defined using the pipe operator as such:
```rust
type BoolOrNum = bool | number

let x: BoolOrNum = false // allowed!
x = 10 // also allowed!
```

They of course don't need to be aliased, and can appear anywhere else a type expression can!
```rust
fn print_arg(x: string | number) {
	print(x)
}

print_arg("hello!") // valid!
print_arg(69)       // also valid!
```

This is also the canonical method for returning optional errors in Bolt:
```rust
fn div(a: number, b: number): number | Error {
	if b == 0 {
		return error("Cannot divide by 0!")
	}
	
	return a / b
}

let x = div(10, 2)
let y = div(10, 0)
```

#### 4. Type testing
The `is` operator in Bolt can be used to determine whether an expression matches a type at runtime, in the form `(expr is T): bool`.
Anytime you're using dynamic typing, whether that be through unions or the `any` type, you can use `is` to determine what the underlying type is.

Using the `div()` example from above:
```rust
let result = div(10, 0)

if result is Error {
	// handle error
}

// there was no error! 
```

#### 5. Optional types
Perhaps the most common use-case for type unions in Bolt is to express "nullable" values - bindings that may conditionally be null - so the language offers a shorthand for these, using the question mark type-operator. This is equivalent to making an union of `T` and `null`.
```rust
type A = number | null
type B = number?

A == B
```

There are a few operators in place specifically to help with nullable values in Bolt, the first is the expect operator `!` where `(expr! where expr is T?): T`. If the expression is non-null, the non-null union variant is returned. If it IS null, a runtime error is raised. This mainly exists for instances where you know logically that something isn't null, but can't prove it programmatically.

```rust
let const maybe: number? = function_that_might_return()

// we know it always returns something now...
let const definite: number = maybe!
```

The second operator is the null-coaslescing operator `??`, it's a binary operator that evaluates the left hand side, and if it is null, returns the right hand side instead. It takes the form `(a ?? b where a is T?, b is T): T`. This is useful for things like optional function parameters, default values, and error recovery.

```rust
fn print_with_prefix(message: string, prefix: string?) {
	print(prefix ?? "Jason:", message)
}

print_with_prefix("Hello!", null) // Jason: Hello!
print_with_pregix("Goodbye..", "Janet:") // Janet: Goodbye..
```

Finally, because checking against null is very common, there's a shorthand for `is null` in the existence operator `?`.

```rust
let const config = get_config_if_exists()
if config? {
	// do things..
}
```

#### 6. Type casting
Determining what type the underlaying value of a binding is is useful, but actually casting and narrowing that type down is just as important! In bolt, the `as` operator is used to perform a runtime-safe cast in the form of `(expr as T): T?`

```rust
let const x = get_num_or_bool()

if x is bool {
	print("We got a boolean!", x as bool!)
} else if x is number {
	print("We got a number!", x as number!)
}
```

Note the use of the expect operator here is safe after we've already made the dynamic type check.

#### 7. Tableshape types
Like mentioned earlier, tables are a very important, fundamental type in Bolt, any type of structured binding is some form of table. In order to really leverage the power of the type system, Bolt allows the definition of type aliases for tables with specific layouts.

```rust
type Person = {
	name: string,
	age: number
}

type Family = {
	address: string,
	members: [Person]
}
```

You can of course still define types for and accept regular, untyped tables. 

```rust
fn takes_any_table(arg: {}) {
	print("Glorb is:", arg.glorb)
}

takes_any_table({glorb: 10}) // "Glorb is: 10"
takes_any_table({blarg: 10}) // "Glorb is: null"
```

Sometimes it can be useful to have tables with partially-specified fields. The `unsealed` keyword allows for this.

```rust
type Document = unsealed {
	title: string,
	size: number,
}

type Header = {
	title: string,
	size: number,
}

let x: Document = { title: "Hello!", size: 16, etc: true } // allowed!
let x: Header = { title: "Hello!", size: 16, etc: true } // error!
```

##### Tableshape construction
Bolt has a dedicated operator for creating strongly typed tables: `=>`. The reason this exists is that it enables both for greater compile-time error and type checking, as well as enabling a level of optimization through deterministic allocation layouts.

```rust
type Vector2 = { x: number, y: number }

let first = Vector2 => { x: 10, y: 10 }         // valid!
let second = Vector2 => { x: 5, y: 5, z: 5 }    // fails at compile-time!
let third = { x: 1, z: 2 } as Vector2!          // fails at runtime...
```

##### Associated functions

It's often useful to associate functions with tableshapes, this can be done by defining functions after the tables layout.

```rust
type Vector2 = { x: number, y: number }

fn Vector2.new(x: number, y: number) {
	return Vector2 => { x: x, y: y }
}

let x = Vector2.new(10, 10)
```

If an associated function's first argument is of the same type as the enclosing table, a sugaring syntax for object-style calls becomes available.

```rust
fn Vector2.add(lhs: Vector2, rhs: Vector2) {
	return Vector2 => {
		x: lhs.x + rhs.x,
		y: lhs.y + rhs.y
	}
}

let y = Vector2.new(5, 5)
let z = x.add(y)
```

Please note that in this case, the following are identical:
```rust
let z = x.add(y)
let z = Vector2.add(x, y)
```

In order to enforce this even closer, the keyword `method` automatically inserts a first argument of the tableshape-type named `this`.

```rust
method Vector2.mul(other: Vector2) {
	// typeof(this) == Vector2

	return Vector2 => {
		x: this.x * other.x,
		y: this.y * other.y
	}
}

let a = z.mul(z)
```

##### Metamethods
Because using methods to implement basic operators is quite common, especially for mathematical types, Bolt offers a set of metamethods that can be defined on tableshapes to overload the regular operator behaviour. 

```rust
method Vector2.@add(other: Vector2) {
	return Vector2 => {
		x: this.x + other.x,
		y: this.y + other.y
	}
}

let b = x + y // actually invokes Vector2.@add!
```

Here's the current list of metamethods in bolt:
* `@add`, `@sub`, `@mul`, `@div` - invoked by the `+ - * /` operators
* `@format` - called whenever the stdlib attempts to convert the object to a string
* `@collect` - called when the object is collected by the garbage collector
* `@eq`, `@neq` - invoked by the `==` and `!=` operator
* `@lt`, `@lte` - invoked by the `< <= > >=` operators

##### `final` modifier
When declaring a tableshape, you can additionally prepend the keyword `final` to the type definition. This is a compile-time-only hint to treat the table's function layout as write-once, discarding some of the dynamic nature of a language like bolt. 

For types that are very inelastic, have very large numbers of method invocations, or otherwise are core to an application, this provides both security in knowing the functionality is immutable, while also allowing the compiler to make much more aggressive optimizations.

```rust
// in module a
type Car = { speed: number }

method Car.drive {
	print("I drove", this.speed, "miles!")
}

// in module b
method Car.drive {
	print("I drove", this.speed * 2, "miles!")
}

// in main
import module_a
import module_b // allowed and fine, Car.drive is overridden!
```

As opposed to

```rust
// in module a
type Car = final { speed: number }

method Car.drive {
	print("I drove", this.speed, "miles!")
}

// in module b
method Car.drive { // error! Car is final and drive is already defined!
	print("I drove", this.speed * 2, "miles!")
}

// in main
import module_a
import module_b // error! there were errors compiling this module
```

##### Extending and combining tables
In order to mimic some basic properties of polymorphism, Bolt allows you to create supertypes of tableshapes using the `+` type-operator.

```rust
type Child = {
	age: number,
	name: string
}

type Adult = Child + {
	children: [Child]
}
```

The base type being extended MUST always be on the left hand side, and doing this also preserves the deterministic order of the table layout, meaning you can pass supertypes as arguments without losing any of the tableshape optimizations.

```rust
fn introduce(c: Child) {
	print("Hello I am", c.name, "and I am", c.age, "years old!")
}

let joey = Child => {
	age: 10,
	name: "Joey"
}

let kenny = Adult => {
	age: 38,
	name: "Kenny",
	children: [joey]
}

introduce(joey)
introduce(kenny)
```

For runtime usecases, the table-composition operator `&` can be used.

```rust
let a = { x: 10 }
let b = { y: 5 }
let c = a & b

print(c.x, c.y) // 10, 5
```

#### 8. Enum types
Oftentimes you'll want a function argument that can only be one of a set of given parameters, and while strings in bolt are interned and thus fast to compare, it leaves room for error and spelling mistakes.

Enums are a simple way to express all variants of a type, and under the hood are converted to an integer representation for memory and performance.

```rust
type Color = enum { Red, Blue, Green }

print_with_col("Hello", Color.Red)
print_with_col("Hello", Color.Blue)
print_with_col("Hello", Color.Purple) // Error, unknown variant!
```

```rust
type AddressType = enum { IPv4, IPv6 }

fn fetch(address: string, kind: AddressType) {
	if kind == AddressType.IPv4 { /* .. */ }
	else if kind == AddressType.IPv6 { /* .. */ }
	else {
		// This code is unreachable, because we can never pass enums
		// aside from the predefined variants
		return error("Ivalid address type!")
	}
}

let const result  = fetch("127.0.0.1:8080", AddressKind.IPv4)
let const result2 = fetch("[::1/128]:8080", AddressKind.IPv6) 
```

#### 9. typeof() and type()
It's common for you to want to have multiple bindings of the same type, or otherwise try to satisfy a constraint between functions. Rather than letting that be up to inference or hard typing, the `typeof()` keyword-operator allows you to programmatically inquire the compile-time typing of any expression.

NOTE: This returns the compile-time known type, and does no dynamic inspection. For union types, the whole union will be returned, regardless of the current variant.

NOTE: Because this is done at compile-time, the expression within the parentheses is not actually evaluated, only type-checked.

```rust
let x = some_function()
let y: typeof(x) = some_other_function() // fails unless they return the same type!
```

```rust
let a: typeof(10) // inferred number! Still default-initialized to 0
let b: typeof(10 + 10) // same thing!
```

```rust
let num: Type = typeof(10) // Can also be used to get a reference to the type itself!
```

```rust
fn serialize(value: any, t: Type) {
	// .. metaprogramming .. 
}

serialize(10, number)

let x = 69
serialize(x, typeof(x))
```

`type()` can also be used in order to pass more complex types by-alias in an inline manner. Normally, when assigning types to bindings or passing them as parameters, identifier-aliases are all that can be used, otherwise.
```rust
fn takes_type(t: Type) {
	..
}

takes_type(string) // ok!
takes_type(string | null) // error! expected expression!
takes_type(string?) // error! operator ? can't apply to Type

takes_type(type(string?)) // this is fine!
takes_type(type(string | null)) // and so is this!
```
## 7. if-else and if let
Conditional expressions and branching code are core to any programming language, and Bolt offers this in a very familiar package. `if`, `else if`, and `else` chains can be used to define such behaviour. Note though, that like many embedded languages, Bolt has no concept of "truthy" expressions - all conditions *must* evaluate to boolean.

```rust
let x = 10

if x < 5 {
	print("this won't happen")
} else if x > 15 {
	print("neither will this...")
} else {
	print("but this will!")
}
```

`if let` is a structure designed to help maintain compactness of code that deals with optional expressions. It defines a binding that *must* infer it's type from the righthand side, which *must* evaluate to an optional type. If the value is `null`, the branch is skipped, else it is entered and the binding is made.

```rust
// given
fn find_file(path: string): File? { .. }

if let file = find_file("bolt.txt") {
	let contents = file.read_all()
	..
} else {
	print("Error: file didn't exist!")
}
```

It's especially useful for dynamic type casting, error handling, and parsing:

```rust
// given the sophisticated man's way
fn find_file(path: string): File | Error { .. }

let result = find_file("bolt.txt")

if let file = result as File {
	..
} else if let err = result as Error {
	..
}
```

```rust
fn serialize(key: string, value: any) {
	if let num = value as number {
		// format number
	} else if let str = value as string {
		// format string
	} else if let boolean = value as bool {
		// format bool
	}
	
	.. etc
}
```

## 8. for
There are four main loop types in bolt, all of which are denoted with the `for` keyword, and are selected based on the expression that follows.

#### 1. for {}
The `for` keyword with no expression prior to the body of the loop is interpreted as being infinite, with no exit condition. In the most literal sense, this will compile down to a body with a jump instruction that returns to the top forever. The only way to terminate a loop like this is with a `break` statement.

```rust
for {
	let const input = read_input()

	if input == "q" { break }

	// .. do something
}
```

#### 2. for (expr) {}
A for loop with a single expression before the body is considered a while-style loop, where the expression is evaluated before every iteration, and the loop is exited if it is `false`. The expression *must* evaluate to a boolean result. 

```rust
let input = read_input()
for input != "q" {
	// .. do something
	input = read_input()
}
```

#### 3. Numeric for 
Purely numeric loops in bolt are expressed with an identifier and a range, with optional components to control things like starting value and stepping. Unless specified, all loops start from 0 and step by 1.

```rust
for i in 0 to 10 by 1 { print(i) } // 0, 1, 2, 3, 4 etc...

for i in 10 { print(i) } // 0, 1, 2, 3, 4 etc...

for i in 10 by 2 { print(i) } // 0, 2, 4, 6, 8, etc...

for i in 10 to 0 by -1 { print(i) } // 10, 9, 8, 7, 6 etc...
```

#### 4. Iterator for
The final form of loop in bolt is the iterator loop. It requires an identifier as well as an iterator function, which has the signature `fn: T?`. An iterator loop will continuously evaluate the iterator until it produces `null`, at which point the loop will exit.

```rust
let iter: fn: number? = [1, 2, 3, 4].each()
for i in iter { print(i) } // 1, 2, 3, 4

fn range(max: number) {
	let i = -1
	return fn {
		i += 1
		if i >= max { return null }
		return i
	}
}

for i in range(100) { .. }
```

If you wish to prevent mutability of the iterated value, you can specify `const`.

```rust
let arr = [{x: 10}, {x: 20}, {x: 30}]
for const item in arr.each() {
	item.x = 10 // error!
}
```

#### 5. `continue` and `break`
Bolt supports the regular `continue` and `break` keywords that exist in most other languages, which prematurely end one iteration of the loop, or exit it entirely. They both are only able to address the innermost loop they reside in, and are not valid outside of a `for` block.

## 9. Modules
Bolt has no concept of global state, everything passed between modules needs to be done through `export` and `import`, both of which take on a few forms.

#### 1. Importing
The simplest imports in Bolt simply mention a module by name, which then acts as a namespace for all it's exports:
```rust
import core

core.print("Hello world!")
```

Specific exports can be queried and pulled from the module through specifying them by name:
```rust
import print from core

print("Hello, world!")
```

In order to simply bring all imports into scope, a wildcard can be used:
```rust
import * from core

print("Hello, world!")
```

Module namespaces are made out of the last part of their import path:
```rust
import nested.module

module.function(10, 20)
```

If module names are cumbersome or otherwise conflict, they can be aliased as well:
```rust
import nested.module as m

m.function(10, 20)
```

By default, module paths are matched against the allowed path-specs literally. In order to import a module from a relative path, quotes can be used:
```rust
import "neighbor"
import "../parent"
import "folder/child"
```

#### 2. Exporting
Exports in bolt take on two primary forms - you can either export a value by passing a single-identifier expression:
```rust
let value = 10
export value

// -- elsewhere --
import value from module
```

Or by following it with an assigning statement:
```rust
export let value = 10
export fn function { print("Hello!") }
export type Structure = {
	field: number,
	name: string
}

// -- elsewhere --
import value, function, Structure from module
```