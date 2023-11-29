# Arrays
Module for utility functions when working with arrays. All functions in this module are availible both as imports and exposed directly on the `array` prototype. That meanas that these are equal:
```rust
import arrays

let arr = [1, 2, 3]

arrays.push(arr, 4)
arr.push(4)
```

---

```rust
arrays.length(arr: [T]): number
```
Returns the length of the array, in number of elements

---

```rust
arrays.pop(arr: [T]): T?
```
Removes and then returns the last item in the array, or `null` if it was empty.

---

```rust
arrays.push(arr: [T], item: T)
```
Appends `item` to the end of `arr`.

---

```rust
arrays.each(arr: [T]): fn: T?
```

Produces an iterator closure that returns each element of `arr` in sequence, followed by `null`.

Example:
```rust
let arr = [1, 2, 3]
for const item in arr.each() {
    print(item) // 1, 2, 3
}
```

---

```rust
arrays.clone(arr: [T]): [T]
```

Creates a perfect copy of the input array.

---

```rust
arrays.reverse(arr: [T]): [T]
```
Reverses all the elements of the input array, in-place.

⚠️ NOTE: This returns the input array back to the caller to allow for method chaining, it does **not** create a copy.

---

```rust
arrays.map(arr: [T], applicator: fn(T): R): [R]
```
Runs `applicator` on every element of `arr`, appending the results to a new array, which is returned.

---

```rust
arrays.filter(arr: [T], filter: fn(T): bool): [T]
```
Applies `filter` to each element of `arr` appending items that it returns `true` for into a new, returned array.

---

```rust
arrays.slice(arr: [T], start: number, length: number): [T]
```
Creates a new array from a subset of `arr`, described by `start` and `length`. Throws a runtime error if this causes an out-of-bounds read.

---

```rust
arrays.sort(arr: [T], comp: fn(T, T): bool): [T]
arrays.sort(arr: [number]): [number]
```
Quicksorts `arr` using the comparison function provided, which is expected to return `true` if the first arugument is smaller than the second. This operation is performed in-place.
An accelerated version for numeric arrays exists, taking no comparison function - this is roughly 8-10x faster.

⚠️ NOTE: This returns the input array back to the caller to allow for method chaining, it does **not** create a copy.