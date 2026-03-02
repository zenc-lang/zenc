# Standard Library: Sort (`std/sort.zc`)

The `sort` module utilizes a highly optimized Polymorphic Macro engine to generate zero-overhead, strictly typed sorting algorithms in C under the hood. It natively implements the `QuickSort` algorithm.

## Usage

Because Zen C compiler handles the macros inside `raw` headers, you do not pass explicit types `::<T>`. You simply invoke the named function matching your primitive.

```zc
import "std/sort.zc"
import "std/vec.zc"

fn main() {
    // Sorting a standard C array
    let arr: int[5];
    arr[0] = 52; arr[1] = 13; arr[2] = 99; arr[3] = 4; arr[4] = 42;
    
    sort_int((int*)arr, 5); // Becomes [4, 13, 42, 52, 99]
    
    // Sorting dynamic collections
    let v = Vec<int>::new();
    v.push(200); v.push(11); v.push(84);
    
    sort_int(v.data, v.length()); // Becomes [11, 84, 200]
}
```

## Structure
By default, the Standard Library exposes explicit explicit function generation for the `int`, `long`, `float`, and `double` scalar types.

## Functions

| Method | Signature | Description |
| :--- | :--- | :--- |
| **sort_int** | `sort_int(arr: int*, len: usize)` | Sorts an array of standard integers `[i32]` in ascending order. |
| **sort_long** | `sort_long(arr: long*, len: usize)` | Sorts an array of long integers `[i64]` in ascending order. |
| **sort_float** | `sort_float(arr: float*, len: usize)` | Sorts an array of floats `[f32]` in ascending order. |
| **sort_double** | `sort_double(arr: double*, len: usize)` | Sorts an array of doubles `[f64]` in ascending order. |

## Custom Object Generation
If you create a custom `struct` equipped with overloading logic for `<` evaluation, you can generate your own zero-overhead sorter simply by triggering the compiler macro `ZC_IMPL_SORT(T)`. 

```zc
// Emits `sort_MyStruct(MyStruct* arr, usize len)`
raw { ZC_IMPL_SORT(MyStruct) } 
```
