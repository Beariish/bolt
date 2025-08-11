## 1. Language Introduction
Bolt is a high-level, embeddable language with a rich type system. It aims to combine the best parts of other scripting environments like Lua or Python while integrating typing directly into the language as opposed to using a preprocessing tool or separate static checker.

The rise of concepts like type hints in Python and Roblox' Luau, as well as the skyrocketing popularity of TypeScript are all large inspirations for Bolt, which aims to take it a step further by leveraging this typing directly in the runtime.

As a result, Bolt is [fast](https://github.com/Beariish/bolt/blob/main/doc/Bolt%20Performance.md), able to omit a lot of dynamic typing steps and function frame setup that other dynamic languages have to deal with. 

Bolt code also becomes a lot more maintainable and scalable, with types providing a mechanism for encoding intent. There are far fewer ways to "break out" of the Bolt type system as opposed to many other dynamic languages with hints, largely due to types being a core part of Bolt's design from the start.

## 2. Example program
Here's a small snippet of bolt code to give a quick taster for the syntax. Don't worry if you can't understand this all yet, it'll be explained later!
```ts
import print, error, Error from core
import abs, epsilon from math

// The return type of safe_divide is inferred to be `Error | number`
fn safe_divide(a: number, b: number) {
    return if abs(b) < epsilon
        then error("Cannot divide by zero!")
        else a / b
}

match let result = safe_divide(10, 5) {
    is Error {
        // The type of result is narrowed in this branch!
        print("Failed to divide:", result.what)
    }

    is number {
        print("The answer is", result)
    }
}
```
At a glance though, it's probably obvious that Bolt borrows a lot of concepts from other contemporary languages. The syntax is pretty strictly C-family styled, but with no semicolons and parentheses around expressions. Bolt's syntax is designed to **never** be ambiguous despite this.

## 3. Who is this for?
This guide isn't intended to teach you the basics of programming or anything of the sort, but rather as an introduction to Bolt's specific syntax and features for someone who already has a fundamental understanding of programming. We won't be covering how all the basic building blocks of a program work, but rather highlighting the parts of Bolt that stand out.

## 4. Static typing and inference
Bolt is a completely statically typed language, meaning the underlying type of a binding cannot ever be changed after it has been declared. In many snippets and examples in this guide, type information will be omitted entirely. It's important to note this **doesn't** mean we're relying on dynamic typing, but rather that Bolt has a very mature and developed type inference mechanism. Types are still present, we're just letting the parser do more of the work.

## 5. Basic literals and fundamental types
Bolt contains only a few basic literal types.
### 5.1. Numbers
All numbers in Bolt are internally expressed as double-width floating-point decimals, and are represented by the `number` type.
They can contain decimal points as well as be proceeded by a minus sign for negation.
```ts
let a = 0
let b = 1
let c = 0.5
let d = -100.25
```
### 5.2. Booleans
Booleans in Bolt are represented by the `bool` type, and can only hold the special values `true` and `false`. Unlike many other high-level languages, Bolt has no concept of "truthiness", and all boolean expressions **must** evaluate to a `bool`.
```ts
let a = true
let b = false
```

### 5.3. Strings
A string literal in Bolt is any number of characters delimited by double quotes, represented by the `string` type. There are **only** double-quoted strings. Common escape sequences are also allowed.
```ts
let a = "this is a string"
let b = "this is a 
multiline string"
let c = "this is also a \n multiline string"
let d = "this string contains \"quotes\" as well!"
``` 

### 5.4. Null
`null` is a very special value in Bolt, in the sense that it is both a fundamental type and the only valid value of that type. We'll go more into detail about how to utilize this later on.
```ts
let a = null
```

## 6. Compound Literals and Containers
Bolt only provides two very simple structures for managing data - arrays and tables. Both of these can be directly assigned through literal expressions, but as they are complex object types actual construction is deferred onto runtime.

### 6.1. Arrays
Arrays in Bolt consist of a comma-separated list of values enclosed in brackets `[]`. They can contain any number of elements, of any type, in any order.
```ts
let a = [1, 2, 3] // An array of numbers
let b = ["hello", "there"] // An array of strings
let c = [true, null, 4] // An array of mixed types!
```
Despite being able to hold values of separate types, arrays in Bolt are still strictly typed. When evaluating the contents, the *narrowest*  type possible to store all elements is selected. The type of an array is denoted by the type of elements surrounded by brackets.
```ts
let a: [number] = [1, 2, 3]
let b: [string] = ["hello", "there"]
let c: [bool | null | number] = [true, null, 4]
```
Additionally, if you wish to enforce a specific type on an array literal, appending the intended element type after a colon in the array body overrides any inference. This can be useful both to check for correctness, as well as intentionally widen the type.
```ts
let a = [1, 2, 3 : number] // No bad data!
let b = ["hello", "there" : any] // Explicitly allow any type!
```

Accessing elements in an array is done with the postfix bracket operator. Values can both be read from and written to this way, but accessing elements outside the array bounds will raise a runtime error. Indices in Bolt always start from 0.
```ts
let a = [10, 20, 30]
a[1] // 20
a[1] = a[0] + a[2]
a[1] // 40
```
Pushing, popping, sorting, and otherwise dealing with arrays in more complex manners is done through the Bolt standard library's [arrays module](https://github.com/Beariish/bolt/blob/main/doc/Bolt%20Standard%20Library/arrays.md).
```ts
let a = [1, 2, 3]
a.push(4)
a.pop() // 4
a.length() // 3
```

### 6.2. Tables
Tables in Bolt hold structured pairs of keys mapped to values, which can then be indexed and updated directly. They're defined between braces `{}` where each key expression is followed by a colon, and then a value.
```ts
let a = { x: 10, y: 20, z: 30 }
let b = { w: "hello", q: true }
```
Any valid Bolt expression can be a table key, and using a bare identifier like above is shorthand for creating a string index.
```ts
let a = { x: 10 }
let b = { "x": 10 } // The same thing!
let c = { 1: "true", true: 5, "hello": false } // mix things up!
```
Much like with arrays, reading and writing to table keys can be done with the postfix bracket operator.
```ts
let a = { x: 10, y: 20 }
a["x"] // 10
a["x"] = a["y"] * 2
```
For string keys, the postfix dot operator can be used as a shorthand.
```ts
let a = { x: 10, y: 20 }
a.x // 10
a.x = a.y * 2
```
The types of tables and all the keys they contain are strictly defined at parse time, meaning that reading or writing outside the predetermined set is not allowed. To explicitly enable this, the `unsealed` keyword can be used.
```ts
let a = unsealed { x: 10 }
a.y = 20 // Even though 'y' wasn't defined, we can assign to it
```
Note that any keys outside the known pairs will yield an `any` when read, as the parser cannot verify that they exist.

### 7. Let and const
As you have probably figured out already reading this far `let` is the keyword used in Bolt to declare a new variable binding. By default, the type of the binding will be inferred from the expression to the right of the assignment, but that inference can be overridden and will error if incorrect.
```ts
let a = 10
let b: number = 20
let c: string = 30 // Error!
```
If the type of a binding is explicitly declared, and no initializing expression is supplied, Bolt will attempt to generate some reasonable default.
```ts
let a: number
a // 0
let b: string
b // ""
```
In order to render a binding immutable, `const` can be supplied after `let`.
```ts
let const a = 10
a += 10 // Error!
```
And unlike many other high level languages, `const` in Bolt prohibits interior mutability as well.
```ts
let const a = { x: 10 }
a.x += 10 // Error!
```
Bolt also allows for bindings to "shadow" other bindings with the same name, as long as they're in a deeper scope.
```ts
let x = 10

if true {
    let x = 20
    x // 20
}

x // 10
```
## 8. Basic Operators
### 8.1. Arithmetic
Bolt supports a very standard set of arithmetic operators, which follow regular mathematic precedence rules. Parentheses can also be used to group sub-expressions and override standard precedence.
```ts
let a = 10
let b = 5 + 10
let c = a * b / 5 - a
let d = -c * (a + b)
```
Arithmetic operators will only apply to the `number` type - anything else is a compile-time error!
```ts
let a = "hello"
let b = a - 2 // Error, cannot sub string and number
```
Compound assignment operators exist as well to modify a binding in-place.
```ts
let a = 1
a += 1
a *= 2
a /= 10
print(a) // 0.4
```
Any more complex math operations are exposed through the [math module](https://github.com/Beariish/bolt/blob/main/doc/Bolt%20Standard%20Library/math.md).

> [!NOTE]  
> print() here is part of the [core module](https://github.com/Beariish/bolt/blob/main/doc/Bolt%20Standard%20Library/core.md). We're going to cover in depth how modules work later, but for now you can assume that all examples have the core and math modules imported.

### 8.2. Comparison
Equality in bolt is expressed by the `==` operator, and the inverse through the `!=` operator. Primitive types (`null`, `number`, `string`, `bool`) are all compared based on their contents, while complex types (arrays, tables) are compared based on identity, unless explicitly overridden by a metamethod - but more on those later.

The operators `<`, `>`, `<=`, and `>=` are defined explicitly only for `number` as well, but can also be overridden through a metamethod. All comparison operators in Bolt produce a `bool` result by default.

```ts
let a = 10
let b = 5
let c = a == b // false
let d = a != b // true
let e = a < b // false

let t1 = { a: 10 }
let t2 = { a: 10 }
let tr = t1 == t2 // false! Same contents, separate identity
```

### 8.3. Concatenation
Bolt does actually define a single instance of `+` outside of adding `number`s, and that's to concatenate `string`s.
```ts
let a = "hello"
let b = a + " " + "world"
print(b) // "hello world"
```

### 8.4. Logical operators
Bolt supports exactly three operators for combinatorial logic - `and`, `or`, and `not`. Since there is no concept of a "truthy" value in Bolt, these **only** operate on the `bool` type.
```ts
let a = 10
let b = 5
let c = a > b and (a < 10 or b == 5) // true
let d = not c // false
``` 

### 8.5. Indexing
`[]` is the main indexing operator in Bolt, taking any valid Bolt value and using it as a key to search a complex type. Whenever the compiler can prove a certain value cannot exist within the object, an error is raised.
```ts
let a = [1, 2, 3]
let b = { x: 10, y: 20, z: 30 }
let c = a[1] // 2
let d = b["y"] // 20
``` 

Like mentioned above, the `.` operator can be used as a shorthand for indexing tables specifically. The identifier after the dot is treated like a string literal.

```ts
let a = { x: 10, y: 20, z: 30 }
let b = a.x + a.y + a.z
```

---
There are a few more operators to cover that interact with the type system in Bolt, but we'll cover those once we have more context.

### 8.6. type() and typeof()
The `type` and `typeof` operators in Bolt are very simple. `type` allows you to explicitly parse a type-expression in a place where a value-expression is expected:

```ts
let a = [number] // Inferred as [Type] containing one element: number
let b = type([number]) // Inferred as Type containing [number]
```

`typeof` instead returns the type of any value-expression within. It should be noted that the expression is never actually evaluated and will have no side effects.

```ts
let a = typeof(10) // a: Type = number
let b = typeof(a) // b: Type = number
let c = typeof(10 + 20 * 6) // c: Type = number, never evaluated
```

And because these constructs are evaluated at parse-time, they can be used in places where runtime values can't.

```ts
fn some_fn(): string { return "hello!" }
let a: typeof(some_fn()) = "world!" // some_fn is never called, but a is deduced to be of type string
```

### 8.7. Null operators
Bolt has a few dedicated operators for dealing with `null`, as it's the main way to express either the lack of a value or a soft error in the language.

The postfix `?` operator can be applied both to types to create a union between them and `null`, as well as to expressions to test for existence.
```ts
let num: number? = 10
let num2: number | null = 10 // The same thing!

if num? {
    // Only enter this block if `num` is not null
    print(typeof(num)) // number - `num` is also narrowed to remove null in this block
}
```

The other postfix operator, `!`, is instead used to explicitly strip `null` from a binding. This can be useful in situations where the programmer knows that a value must exist, or that the lack of a value is an unrecoverable error, since a runtime error is raised if the value is `null`.
```ts
let num: number? = 10
let num2: number? = null

let n: number = num!
let n2: number = num2! // Runtime error, num2 is null
```

The binary null-coalescing operator `??` can be used to select between two values. The resulting expression will have the narrowest union between the operands, sans `null`.
```ts
let num: number? = 10
let n = num ?? 20 // n is 10, since num exists

let num2: number? = null
let n2 = num2 ?? 20 // n2 is 20, since num is null
```

Finally, the null-indexing operator `?.` can be used to walk a tree of tableshape properties without needing to worry about if they exist.
```ts
let t = { x: { y: 10 } }
let t_opt: typeof(t)? = t

let n = t?.x.y // n is number | null
```

## 9. If, if else, and else
If-statements provide a mechanism to express conditional execution in Bolt, and they function almost identically to most other languages. It's important to note that Bolt doesn't have any "truthy" values, though, and all expressions *must* evaluate to the `bool` type.
```ts
let const x = 10
if x > 5 {
    // do things
}
```

Optionally, an else-expression can follow the block.
```ts
let const x = 10
if x < 5 {
    // never happens
} else {
    // do things
}
```

Any number of if-else chains can follow as well.
```ts
let const x = "hello"
if x == "bye" {
    // never happens
} else if x == "goodbye" {
    // never happens
} else if x == "hello" {
    // do things
} else {
    // never happens
}
```

If the block following an if-statement consists of a single expression, the `then` keyword can be used in place of a pair of braces.
```ts
let const x = 10
if x == 10 then print("x is ten!")
```
Additionally, a single expression is allowed to immediately follow `else`
```ts
let const x = 10
if x > 10 then print("x is large!")
else print("x is small!")
```

### 9.1. If let
The if-let construct in Bolt allows you to express a branch that is only taken if a value is non-null. This is useful for things like conditional casting, optional arguments, or unset fields.
```ts
fn maybe_number(): number | null { return 10 }

if let x = maybe_number() {
    print(x) // 10, x is `number` here!
}

print(x) // Error, x only exists within the block!
```

These can of course be mixed with other if-constructs as well.

```ts
fn maybe_number(): number | null { return 10 }

if 10 < 5 { 
    print("This will never happen") 
} else if let x = maybe_number() {
    print(x) // 10, x is `number` here!
} else {
    print("This will never happen")
}
```

The expression being assigned to the local binding must be nullable, too.
```ts
let num = 10
if let x = num { // Error: Expression type must be nullable
    print(x)
}
```

### 10. Casting operators
There are two operators in Bolt responsible for checking and converting the types of values - `is` and `as`.

`is` will check whether an expression evaluates to a value of a given type, producing a boolean answer. Whenever it's used as the sole operator in a branching statement, the binding is narrowed within the enclosing block.
```ts
let a: number | bool = 10
if a is number {
    // Only enter the block if 'a' is a number
    a += 10 // Legal, as a has been narrowed to number
}
```

`as` will attempt to extract the value and treat it as the type provided. It will **not** make a copy of any complex object, and will not perform any destructive conversion on the original. The resulting type of `x as T` is `T?`, as `null` is produced whenever the cast fails.

```ts
let a: number | bool = 10
let b = a as bool // b: bool? = null
let c = a as number! // a: number = 10, use ! to strip the null

if let num = a as number {
    // safe cast, we skip the branch if a is not a number
}
```

## 11. Loops
There are three basic types of loops in Bolt, all of which are identified by the `for` keyword. The types of the expressions provided help Bolt determine which kind of loop to execute.
### 11.1. Numeric for
Numeric loops in Bolt function similarly to loops in any C-style language, albeit with slightly different syntax - this is because the interpreter uses specialized instructions under the hood to enhance performance. All loops operate on a semi-inclusive range `[begin, end)`.

The most simple numeric loop may look something like this:
```ts
for i in 10 {
    print(i) // 0 1 2 3 4 5 6 7 8 9
}
```
As you can see, the lower bound of the loop is inferred to be `0`, and the step size is `1`, but both of these things can be controlled:
```ts
for i in 5 to 10 {
    print(i) // 5 6 7 8 9
}

for i in 10 by 2 {
    print(i) // 0 2 4 6 8 
}

for i in 100 to 1000 by 250 {
    print(i) // 100 350 600 850
}
```

The values provided can of course also come from any expression, and not just numeric literals:
```ts
let const min = 0
let const max = 10
let const step = 2

for i in min to max by step {
    print(i) // 0 2 4 6 8
}

for i in max - step * 3 to max * 2 by step + 1 {
    print(i) // 4 7 10 13 16 19
}
```

All variations of the `for` loop also support using the `do` keyword in place of braces for the body, as long as it consists of a single expression.

```ts
for i in 10 do print(i)
```

### 11.2. While-style for
If a single boolean expression is provided, a loop in Bolt behaves similarly to how a `while`-loop traditionally does. As long as the loop condition evaluates to `true`, the body will be executed.

```ts
let num = 100

for num > 10 {
    num -= 1
}

print(num) // 10
```

### 11.3. Infinite for
Additionally, if no expression is provided at all, the loop is simply run infinitely. 
```ts
for { /* Never exits */ }
// Equivalent to 
for true { /* Never exits */ }
```

### 11.4. Iterator for
The final looping construct in Bolt is the iterator loop. We'll cover how to actually implement an iterator function later on, but for now know it simply produces a series of values when called.
```ts
let arr = [10, 20, 30, 40]
for item in arr.each() {
    print(item) // 10 20 30 40
}
```

The `const` keyword can also be added to prevent mutability of the iteratee.
```ts
let arr = [{x: 10}, {x: 20}, {x: 30}]
for const item in arr.each() {
    item.x *= 2 // Error, `item` is const!
}
``` 

### 11.5. Continue and break
Loop evaluation can be manually altered through the use of the `continue` and `break` keywords. 

`continue` will skip the remainder of the loop body and then resume at the next iteration:
```ts
for i in 10 {
    if i < 5 { continue }
    print(i) // 5 6 7 8 9
}
```

`break` will completely skip the rest of the loop:
```ts
for i in 10 {
    if i == 5 { break }
    print(i) // 0 1 2 3 4
}
```

## 12. Match
The `match` statement allows you to compare an expression against many diverging branches in a nice way, walking down the list of expressions one by one until a branch is taken.

```ts
let x = get_random_number()
match x {
    1 { print("x is small!") }
    2 { print("x is larger!") }
    3 { print("x is largest!") }
}
```

If none of the branches match, a special `else` branch can be included as a fallback.

```ts
let x = get_random_number()
match x {
    1 { print("x is small!") }
    2 { print("x is larger!") }
    3 { print("x is largest!") }
    else { print("x is unknown!") }
}
```

Multiple possible expressions can be evaluated for the same branch as a comma-separated list.

```ts
let x = get_random_number()
match x {
    1, 2, 3 { print("x is small!") }
    4, 5, 6 { print("x is larger!") }
    7, 8, 9 { print("x is largest!") }
    else { print("x is unknown!") }
}
```

What's really happening under the hood here is that the expression `x == ` is being implicitly generated on the left hand of each branch expression. You can override this behaviour by supplying an operator of your own.

```ts
let x = get_random_number()
match x {
    < 5 { print("x is small!") }
    5 { print("x is exactly five!") }
    > 5 { print("x is large!") }
}
```

This can be very useful for matching based on the type of a union binding.

```ts
let x: number | bool | string = get_union()
match x {
    is number {
        x += 10 // Narrowed and usable as a number
    }

    is bool {
        print("x is", x)
    }

    is string {
        print("x is a string!")
    }
}
```

If you wish to omit the generated expression entirely, surround the condition with parentheses.

```ts
let x = get_random_number()
match x {
    < 5 { print("x is small!") }
    (is_even(x)) { print("x is even!") }
}
```

And of course, you can mix and match all of these forms within a match statement.

```ts
let x = get_random_number()
match x {
    < 5, 6, (is_even(x)) { print("x is small, exactly 6, or even!") }
}
```

Just like with `if`-statements, `then` is a valid way to declare a branch body, as long as it consists of a single expression. A trailing comma can be used so the parser understands where one expression ends and the next branch begins.

```ts
let x = get_random_number()
match x {
    1 then print("x is 1!"),
    2 then print("x is 2!"),
    3 then print("x is 3!")
}
```

### 12.1. Match let
Similar to `if let`, there's a `match let` variation that exists to give a temporary binding to the expression being matched on. This is useful for when the value is only relevant to the chosen branch.

```ts
match let x = get_random_number() {
    < 5 { print("x is small!") }
    (is_even(x)) { print("x is even!") }
}

print(x) // Error: no binding named x
```

## 13. If, for, and match expressions
The `if`, `for`, and `match` constructs we just covered can also be used as expressions in Bolt, letting you express conditional logic inline with the rest of your code, and avoid situations where you have undefined bindings or complex branching to pass the correct values along to functions.

### 13.1. if-expressions
If-expressions are very simple, following the same branching rules as if-statements as described above. The value they result in is the final expression in whichever branch is taken, or `null` if the final expressions produces nothing. The type produced, however, is the narrowest possible union of every branch.

```ts
let x = 10
let is_x_large = if x > 5 then true else false

let is_x_large_as_blocks = if x > 5 {
    print("x is large!")
    true
} else {
    print("x is small!")
    false
}
```

In the absence of an `else` branch, `else null` is implicitly generated.

```ts
let x = 3
let is_x_large = if x > 5 then true // is_x_large: bool? = null
```

Any number of `else if` branches can also be included.

```ts
let x = get_random_number()
let x_quantized = 
    if x < 2 then 0
    else if x < 4 then 2
    else if x < 6 then 4
    else 6
```

### 13.2. for-expressions
For-expressions in Bolt are very similar to list comprehensions in other languages, where the resulting value from the loop body is collected into an array.

```ts
let evens: [number] = for i in 10 do i * 2
print(evens) // 0 2 4 6 8 10 12 14 16 18
```

The loop body can be any length, and include things like `continue` and `break` while working as expected.

```ts
let filtered = for n in nums.each() {
    if ispow2(n) { continue }
    if n > 1000 { break }
    n / 2
}
```

### 13.3. Match-expressions
Match-expressions are extremely similar to if-expressions in how they function, but all the same semantics of `match` apply.

```ts
let x = get_random_number()
let desc = match x {
    1, 2, 3 then "x is small",
    > 5 then "x is large!",
    else "x is weird."
}
```

## 14. Functions
Functions are a key component when it comes to structuring your program, and we've been calling a few of them already in order to demonstrate other language concepts above. Functions in Bolt are first-class objects that can be passed around like values, but they have very strict typing and arity at the same time.

Let's look at a simple Bolt function and break it down:
```ts
fn add(a: number, b: number): number {
    return a + b
}
```

Just like explicitly typed bindings, the type of each argument is supplied after a colon, and a colon after the argument list is where the return type of the function is specified. The `fn` keyword at a statement level like this is actually just syntactic sugar for making a constant binding, meaning the below is exactly equivalent:

```ts
let const add = fn(a: number, b: number): number {
    return a + b
}
```

If a function returns nothing, the return type can be left off entirely:
```ts
fn log(a: number) {
    print(a)
}
```

Or explicitly denoted with the special non-type, `!`:
```ts
fn log(a: number): ! {
    print(a)
}
```

The parameter list itself is completely optional if no parameters are present:
```ts
fn print_thing {
    print("thing!")
}

fn get_ten: number {
    return 10 
}
```

The simplest possible Bolt function, that takes no arguments, returns nothing, and does nothing looks like this:
```ts
let const does_nothing = fn {}
```

### 14.1. Return type inference
More often than not, Bolt can infer the return type of a function and the programmer doesn't need to notate it at all. Anytime the return type of a function isn't explicitly provided, this is what the parser does, and the evaluated type becomes the narrowest union of all returning branches in the function body.

This is **not** dynamic typing by any means, the return type is still strong.

```ts
fn add(a: number, b: number) {
    return a + b
}

let result = add(10, 20) // result: number = 30
```
### 14.2. Recursion
Recursion in Bolt is only supported for functions that can be fully declared beforehand, meaning they're made through statement-level `fn` and with a fully declared return type. 

```ts
fn fib(n: number): number {
    return match n {
        0, 1 do 1,
        else fib(n - 1) + fib(n - 2)
    }
}

let result = fib(16) // result: number = 1597
```
### 14.3. Closures
Functions in Bolt aren't limited to addressing their arguments and module imports, but can also pull in context from outside scopes. Closures are created dynamically at the site of their assignment, and copy references into the upvalue buffer immediately - this means that unlike some languages, simple types like numbers and booleans can't be shared from within a closure and outside.

```ts
fn make_counter {
    let count = 0
    return fn {
        count += 1 // count behaves like internal state here, captured from the surrounding scope
        return count
    }
}

let counter = make_counter()
print(counter()) // 1
print(counter()) // 2
print(counter()) // 3
```

Complex object types are captured by reference, though, meaning they can be accessed from multiple closures.

```ts
let counter_state = { num: 0 } // Note that this is declared outside, so we're not making a new table for each
fn make_shared_counter {
    return fn {
        counter_state.num += 1
        return counter_state.num
    }
}

let counter1 = make_shared_counter()
print(counter1()) // 1
print(counter1()) // 2
print(counter1()) // 3

let counter2 = make_shared_counter()
print(counter2()) // 4
print(counter2()) // 5
print(counter2()) // 6
```

### 14.4. Iterators
An iterator function in Bolt, as in the kind that can be used in the `for .. in .. {}` construct, is actually not all that special. They simply have to match the signature `fn: T?` where `T` is the type being iterated over. The loop will call the function repeatedly until it produces `null`. 

Here's an example:
```ts
fn range(max: number): fn: number? {
    let index = -1 // We always increment, to start one below
    return fn {
        index += 1
        return if index >= max then null else index
    }
}

for i in range(10) {
    print(i) // 0 1 2 3 4 5 6 7 8 9
}
```

## 15. Complex types
Bolt's type system is deep and rich, and there is plenty more we can do with our types than we've shown off so far.
### 15.1. The 'any' type
There's a special, final fundamental type in Bolt: `any`.
A binding with type `any` can, quite predictably, contain any valid Bolt value. You cannot perform any operations on an `any`, as it must be downcast into a concrete type first, but it can be useful to emulate some polymorphic behaviour.

```ts
fn print_type(x: any) {
    match x {
        is number then print("number"),
        is string then print("string"),
        is bool then print("bool"),
        else print("unknown")
    }
}

print_type(10)
print_type("hello")
print_type({ x: 10 })
```
### 15.2. Aliases
Aliases can be defined using the `type` keyword at the statement level. This is how you define types to be reusable and accessible from multiple locations. *Any* valid type expression can be turned into an alias.
```ts
type MyNumber = number
type MaybeString = string?

let x: MyNumber = 10
let y: MaybeString = null
```
#### 15.2.1. Signatures
Defining aliases for function signatures is a great way to handle things like events and callbacks too. A signature type is defined almost exactly like a function itself, except with no names.
```ts
type NumberOperator = fn(number, number): number

// All of these are valid
fn add(a: number, b: number) { return a + b }
let const sub = fn(a: number, b: number) { return a - b }
let const mul: NumberOperator = fn(a: number, b: number) { return a * b }

fn apply(a: number, b: number, op: NumberOperator) {
    return op(a, b)
}

// All of these are valid!
apply(10, 20, add)
apply(10, 20, sub)
apply(10, 20, mul)
apply(10, 20, fn(a: number, b: number) { return a / b })
```
### 15.3. Unions
We've touched on unions a few times already at this point, but to formally define them, a union type can simply contain one of  a subset of types. Operations cannot be performed on unions directly, and casting/narrowing to a known type is required first. In a similar sense, you can think of `any` as a union of *all* types.
```ts
type NumBoolString = number | bool | string

let x: NumBoolString = 10
let y: NumBoolString = false
let z: NumBoolString = "hello!"

fn takes_uni(u: NumBoolString) {
    match u {
        is number { ... }
        is bool { ... }
        is string { ... }
    }
}

takes_uni(x)
takes_uni(y)
takes_uni(z)
```

Union types can also be recursive to express more complex data structures.
```ts
type JsonValue = number
               | string
               | bool
               | null
               | [JsonValue]
               | { ..string: JsonValue }
```
#### 15.3.1. Nullable
We've also touched on nullability a few times - something being "nullable" in Bolt simply means that it's a union containing null. All the `?` type operator does is append `| null` to the end of a union. As `any` is a union of all types, it is also nullable.

### 15.4. Tableshapes
Tableshape types let us predefine the layout of tables so that we can work with a common interface throughout our code. In its simplest form, we can simply provide all the keys and their types when defining our tableshape.

```ts
type Vec2 = {
    x: number,
    y: number
}

let v: Vec2 = { x: 10, y: 20 }
```

We can of course store any complex type within a tableshape, too.
```ts
type Node = {
    parent: Node?
    children: [Node]
}

let root: Node = { 
    parent: null, 
    children: [
        { parent: root, children: [:Node] },
        { parent: root, children: [:Node] }
        : Node
    ]
}
```

All fields in a tableshape are very strictly required, too. Partial tables will not satisfy the type requirement.

```ts
type ABC = { a: number, b: number, c: number }
let a: ABC = { a: 10 } // Error, cannot cast to ABC!
```
#### 15.4.1. Fat arrow
The tableshape construction operator, or "fat arrow", can be used to explicitly declare the type of a table literal. This often yields better performance, catches more type errors at pars-time, and also applies the tableshape's prototype to the new table, which will be relevant soon.
```ts
type Vec2 = {
    x: number,
    y: number,
}

let v = Vec2 => { x: 10, y: 20 } // v is inferred to specifically be Vec2
```
#### 15.4.2. Prototypes
Tableshape prototypes allow us to define functions that are associated with a specific type. You might have noticed above how we use `.each()` on arrays, for example - that's a prototype function! Defining our own prototype functions is simple, we simply use the type's name as part of the function declaration.

```ts
type Vec2 = {
    x: number,
    y: number,
}

// It's considered good practice to give complex types a .new()!
fn Vec2.new(x: number, y: number) {
    return Vec2 => { x: x, y: y }
}

let v = Vec2.new(10, 20)
```

If the first argument to a prototype function is of the same type as the prototypical type itself, it can be called as if it was a member on an object of that type.

```ts
fn Vec2.display(v: Vec2) {
    print("x:", v.x, "y:", v.y)
}

v.display() // v is implicitly passed!
```

The special `this` keyword exists to shorthand this, but it's only syntactic sugar.

```ts
fn Vec2.length(this) {
    return math.sqrt(this.x * this.x + this.y * this.y)
}

let l = v.length()
```

Prototype functions can refer to each other, but only as long as they follow the order they've been declared in.

```ts
fn Vec2.static_length(x: number, y: number) {
    let v = Vec2.new(x, y)
    return v.length()
}
```

Recursion is also supported with prototype functions, given the same caveats as with freestanding functions. No type inference is allowed in the declaration!

#### 15.4.3. Metamethods
Some prototype functions are special, and allow you to override the default behaviour in Bolt in certain cases. These all have names starting with the reserved metamethod symbol, `@`.

```ts
fn Vec2.@add(this, other: Vec2) {
    return Vec2.new(this.x + other.x, this.y + other.y)
}

let v1 = Vec2.new(5, 5)
let v2 = Vec2.new(10, 10)
let v3 = v1 + v2 // v3.x = 15, v3.y = 15
```

As of right now, bolt supports:
* `@add`, `@sub`, `@mul`, and `@div` for arithmetic operations. (+ - * /)
* `@lt` and `@lte` for ordering comparisons. (< <=)
    * Note that there's no `@gt`, Bolt reorders operands.
* `@eq` and `@neq` for equality comparisons. (== !=)
* `@format` for stringification. This is invoked by standard library functions like `to_string()` and `print()`
#### 15.4.5. Unsealed
Just like with table literals, the `unsealed` keyword indicates that keys not normally present in a tableshape type are allowed to be written to and read from. Any time you access a key that isn't explicitly defined, you get an `any` back, which will be `null` if the key is not set.

```ts
type Vec2 = unsealed { x: number, y: number }

let v = Vec2 => { x: 10, y: 20 }
v.z = 10 // Allowed!

print(v.x, v.y, v.z) // 10 20 10
print(typeof(v.x), typeof(v.y), typeof(v.z)) // number number any
print(v.w) // null
```
#### 15.4.6. Final
Declaring a tableshape with the `final` keyword does a few things:
* It disables extension for this type
* It makes the prototype write-one, disallowing function interception
* It cannot be combined with `unsealed`

But what this does is allow the Bolt compiler to make more aggressive optimizations when interacting with this type, doing things like fetching prototype functions ahead of time and avoiding dynamic lookups. Fundamental helper types, like math or physics structures, can benefit a lot from this.

```ts
type Angle = final { rad: number }

fn Angle.from_deg(deg: number) {
    return Angle => { rad: deg / 180 * math.pi }
}

// .. elsewhere
let old_deg = Angle.from_deg
fn Angle.from_deg(deg: number) { // Not allowed if Angle is final!
    print("converting:", deg)
    return old_deg(deg)
}
```

#### 15.4.7. Extension
The table extension type operator `+` can be used to emulate some basic features of inheritance. It creates a new type that contains all the fields defined in the base type, while also linking their prototypes together.

Only one type can be extended from at a time. Extended types cannot have `final` or `unsealed` qualifiers.

```ts
type Base = {
    name: string,
}

fn Base.display(this) {
    print("I am", this.name)
}

fn Base.shout(this) {
    print("I AM", this.name, "!!!!")
}

type Derived = Base + {
    height: number
}

// Override Base.display
fn Derived.display(this) {
    Base.display(this) // Explicitly call base function
    print("And I am", this.height, "tall!")
}

let b = Base => { name: "John" }
b.display() // "I am John"
b.shout() // "I AM John !!!!"

let d = Derived => { name: "Jim", height: 170 }
d.display() // "I am Jim And I am 170 tall!"
d.shout() // "I AM Jim" - callable even though it's never defined on Derived

fn display_guy(b: Base) {
    b.display()
}

// Even when downcast to a base type, the derived versions' functions are invoked
display_guy(b) // "I am John"
display_guy(d) // "I am Jim And I am 170 tall!"
```

#### 15.4.8. Dictionary
Dictionary table types are very similar to unsealed tables, but they explicitly specify the types of the key-value pairs inside. They can be indexed like any normal table, and return nullable values of the type specified,

```ts
type StringToNum = { ..string: number }

let s2n: StringToNum = {
    "one": 1,
    "ten": 10,
    "twelve": 12,
}

print(s2n["one"]) // 1
print(s2n["two"]) // null
print(typeof(s2n["twelve"])) // number?
```
#### 15.4.9. Defaults
Most types in Bolt can generate reasonable default values if none are supplied.
```ts
let n: number // = 0
let s: string // = ""
let b: bool // = false
let a: any // = null
.. etc
```
And this includes tableshapes, as long as all the members can generate defaults.

```ts
type T = {
    n: number,
    s: string,
    b: bool,
    a: any,
}

let t: T // this is fine!
```

Tableshape types can also be given explicit default values, as long as they are literal.
```ts
type T = {
    n: number = 10,
    s: string = "hello!",
    b: bool = true
    a: any = "wow!"
}

let t: T
print(t.n, t.s, t.b, b.a) // 10 "hello!" true "wow!"
```

You can of course also override these at all.
```ts
let t: T = { n: 100, s: "bye!", b: false, a: 20 }
let t2: T = { s: "bye!", a: 20 } // override just a subset
```
### 15.5. Enums
Bolt enums are very simple, being represented by an integer under the hood, but still come in handy when it comes to expressing which variant of something is selected. They're defined as follows:
```ts
type Color = enum {
    Red, Green, Blue
}

let c = Color.Red
```

You can cast between enum types and numbers as well, but the cast will fail if the number lies outside the valid range of enum values.
```ts
type Color = enum { Red, Green, Blue }

let c = 1 as Color!
print(c) // Color.Green
print(c as number!) // 1

let c2 = 10 as Color
print(c2) // null
``` 
#### 15.5.1. Unsealed
Enum types can also be declared as `unsealed` to explicitly allow values outside of the defined range to be represented. This can be useful to use enums as opaque ID types, for example.

```ts
type ID = unsealed enum { Invalid }

let id: ID // ID.Invalid
let id2 = 100 as ID // Fine! No need to un-null, as all casts are valid here
```

## 16. Modules
All Bolt code is executed in the context of a module. In the simplest sense, each Bolt source file is considered a module, completely isolated from the rest of the program. All native functionality exposed to Bolt is done through the module system as well.
### 16.1. Importing
There are a few ways to import a module in Bolt. 

Importing native modules, like the ones in the standard library, is done directly through an identifier.
```ts
import core
import math
import io
```
This makes all the exported names in these modules accessible through the module namespace.

```ts
core.print("hello!")
math.sqrt(10)
```

In case of conflict or convenience, you can give modules an alias as well.
```ts
import core as c

c.print("this is okay!")
core.print("not this, as core is not defined anymore!")
```

Specific imports can also be extracted from the module.
```ts
import print, write, error from core

print("This acts as a local, now!")
```

Or you can supply a wildcard to import all names.
```ts
import * from core

print(error("Everything is in scope!"))
```

Other Bolt source files can also be imported this way, and a file with the same name as the module will be searched for according to the configured import paths. Periods can be used to organize submodules or search in directories as well.
```ts
import * from engine.graphics.draw // Will find either a native module with this exact name, or search for engine/graphics/draw.bolt
```

Sometimes, for organization, it can be more convenient to think of source files as relative to each other, surrounding the module name in quotes allows the user to treat it as a path.
```ts
import * from "module" // Search for "module.bolt" in the current directory
import * from "subfolder/module"
import * from "../parent"
```
### 16.2. Exporting
Exporting values from a Bolt module is very simple.

```ts
let to_export = 10
export to_export
```

And then, in another module..
```ts
import my_module

print(my_module.to_export) // 10
print(typeof(my_module.to_export)) // number
```

Any valid Bolt value can be exported. The `export` keyword can also be combined with any binding-generating statement to combine the two steps into one.

```ts
export let to_export = 10

export fn exported_function(n: number) {
    return n * 2
}

export type ExportedType = unsealed {
    name: string,
    callbacks: [fn]
}
```
### 16.3. Standard modules
The Bolt standard library contains a bunch of useful modules, though it's up to the host application to decide which are included. Their contents can be found [here](https://github.com/Beariish/bolt/tree/main/doc/Bolt%20Standard%20Library).

## 17. Learn more

If you wish to learn more, I highly recommend you check out the Bolt [examples](https://github.com/Beariish/bolt/tree/main/examples), or even dive into the [benchmarks](https://github.com/Beariish/bolt/tree/main/benchmarks) or [tests](https://github.com/Beariish/bolt/tree/main/tests) to get a deeper understanding for the language. The rest of the documentation can be found [here](https://github.com/Beariish/bolt/tree/main/doc), as well.

