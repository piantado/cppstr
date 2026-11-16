A number of functions to make C++ friendlier at printing and string conversion. Header-only, C++20.

`str` will recursively map different types to strings (including vectors, maps, etc., vectors of vectors, etc.).

`str_to<T>` does the reverse, mapping strings back into the objects, including nested containers.

`print` allows for friendlier, python-like printing using `str` (arguments separated by a tab, thread-safe).

## Formats

| Type                 | `str` output        | `str_to` also accepts           |
|----------------------|---------------------|---------------------------------|
| vector, deque        | `[a,b,c]`           | `a,b,c`                         |
| set, multiset        | `{a,b,c}`           | `a,b,c`                         |
| map                  | `{k:v,k:v}`         | `k:v,k:v`                       |
| pair                 | `<a,b>`             | `a:b`                           |
| tuple, array         | `<a,b,c>`           | `a,b,c`                         |
| float/double         | shortest exact form, e.g. `0.30000000000000004` | |
| int8_t / uint8_t     | as numbers (`65`, not `A`) | |
| bool                 | `1` / `0`           | `1`, `t`, `true`, `True` are true; anything else is false |

`str_to<T>(s, delim)` uses `delim` instead of `,` for the top-level split only (e.g. `'\t'` for TSV lines).

## Round trips

`str_to<T>(str(x)) == x` for arithmetic types, strings, and the containers above nested arbitrarily,
**except** when:

- a string inside `x` contains any of `, : [ ] { } < >` (there is no quoting or escaping);
- a vector/deque/set holds just one empty string (`[]` is read as an empty container);
- a floating-point value is subnormal (e.g. `1e-310`): `std::stod` throws `std::out_of_range` for these.

Number parsing uses `std::stoi`/`std::stod` etc., so it is lenient: leading whitespace is skipped,
trailing junk is ignored (`str_to<int>("3.7") == 3`), and parsing depends on the C locale.

## Type names

`type_str<T>()` names a *type* (whereas `str(x)` names a *value*), using the same vocabulary as `str`:

    type_str<std::map<std::string, std::vector<double>>>()  // "map<string,vector<double>>"
    type_str<const int* const&>()                           // "const int* const&"
    type_str<int[2][3]>()                                   // "int[2][3]"
    type_str(x)                                             // name of x's type, without top-level const

Fundamental types, `std::string`, and the containers above (with default allocators/comparators)
get short names that are the same on every compiler; `const`, `volatile`, `&`, `&&`, pointers and
arrays are kept. Anything else (user types, other std types, customized containers, pointers or
references to arrays/functions) falls back to the compiler's own spelling, which differs between
compilers (e.g. `std::__cxx11::list<int>` on GCC vs `std::list<int>` on Clang). Typedefs are not
preserved (`int64_t` is `long` on Linux, `long long` on Windows). Tested on GCC and Clang; on
other compilers the fallback uses `typeid`, which drops cv/ref qualifiers.

## Tests

    cd tests && g++ -std=c++20 -g -Wall -Wextra -fsanitize=address,undefined -I.. test_cppstr.cpp second_tu.cpp -o test_cppstr && ./test_cppstr

builds and runs the tests under AddressSanitizer and UBSan (also works with clang++ and -std=c++26).
