# Tables

This module contains functions for ease of interaction with tables and tableshapes.

## Types
```ts
type Pair<K, V> = {
    key: K,
    value: V
}
```

## Functions

```ts
// Creates an iterator that runs over all key-value pairs of `t`, returning `null` after the last one. 
// If `t` is a dictionary-like table, the fields of the resulting pair are strongly typed.
/** Examples:
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
------------------------------------------------------------
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
*/
tables.pairs(t: table): fn: tables.Pair?

// Searches for and deletes entry `key` in `t`, returning whether or not it was found.
// This function will fail to monomorphise for any sealed table types, 
// but beware that deleting known keys from unsealsed tableshapes is also unsafe.
tables.delete(t: table, key: any): bool

// Returns the number of key-value pairs in the table
tables.length(t: table): number
```
