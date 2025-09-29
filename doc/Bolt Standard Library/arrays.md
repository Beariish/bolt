# Arrays
Module containing utility functions for working with arrays. All functions in this module are availible both as imports and exposed directly on the `array` prototype. That means that these are equal:
```ts
import arrays

let arr = [1, 2, 3]

arrays.push(arr, 4)
arr.push(4)
```

## Functions

```ts
// Returns the length of the array, in number of elements
arrays.length(arr: [T]): number

// Removes and then returns the last item in the array, or `null` if it was empty.
arrays.pop(arr: [T]): T?

// Appends `item` to the end of `arr`.
arrays.push(arr: [T], item: T)

// Preallocates at least `n` slots in `arr`
arrays.reserve(arr: [T], n: number)

// Produces an iterator closure that returns each element of `arr` in sequence, followed by `null`.
/** Example:
    let arr = [1, 2, 3]
    for const item in arr.each() {
        print(item) // 1, 2, 3
    }
*/
arrays.each(arr: [T]): fn: T?

// Creates a perfect copy of the input array.
arrays.clone(arr: [T]): [T]

// Reverses all the elements of the input array, in-place.
// ⚠️ NOTE: This returns the input array back to the caller to allow for method chaining, it does not create a copy.
arrays.reverse(arr: [T]): [T]

// Runs `applicator` on every element of `arr`, appending the results to a new array, which is returned.
arrays.map(arr: [T], applicator: fn(T): R): [R]

// Applies `filter` to each element of `arr` appending items that it returns `true` for into a new, returned array.
arrays.filter(arr: [T], filter: fn(T): bool): [T]

// Creates a new array from a subset of `arr`, described by `start` and `length`. 
// Throws a runtime error if this causes an out-of-bounds read.
arrays.slice(arr: [T], start: number, length: number): [T]

// Quicksorts `arr` using the comparison function provided, which is expected to return `true` 
// if the first arugument is smaller than the second. This operation is performed in-place.
// ⚠️ NOTE: This returns the input array back to the caller to allow for method chaining, it does not create a copy.
arrays.sort(arr: [T], comp: fn(T, T): bool): [T]

// An accelerated version for numeric arrays, taking no comparison function - this is roughly 8-10x faster.
// ⚠️ NOTE: This returns the input array back to the caller to allow for method chaining, it does not create a copy.
arrays.sort(arr: [number]): [number]

// Concatenates `result` with `arrs`, modifying it in-place
arrays.concatenate(result: [T], arrs: ..[T])

// Flattens an N-dimensional array into an (N-1)-dimensional array
arrays.flatten(arrays: [[T]]): [T] 
```