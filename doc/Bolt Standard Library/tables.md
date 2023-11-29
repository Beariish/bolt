# Tables

This module contains functions for ease of interaction with tables and tableshapes.

---

```rust
tables.pairs(t: table): fn: Pair?
```

Creates an iterator that runs over all key-value pairs of `t`, returning `null` after the last one. 
If `t` is a dictionary-like table, the fields of the resulting pair are strongly types.

Examples:
```rust
let t = {
    x: 10,
    y: 20,
    z: "hello"
}

for const pair in tables.pairs(t) {
    print(typeof(pair.key))   // type(any)
    print(typeof(pair.value)) // type(any)
    print(pair.key)           // "x", "y", "z"
    print(pair.value)         // 10, 20, "hello"
}
```
```rust
let t: { ..string: number } = {
    x: 10,
    y: 20,
    z: 30
}

for const pair in tables.pairs(t) {
    print(typeof(pair.key))   // type(string)
    print(typeof(pair.value)) // type(number)
    print(pair.key)           // "x", "y", "z"
    print(pair.value)         // 10, 20, 30
}
```