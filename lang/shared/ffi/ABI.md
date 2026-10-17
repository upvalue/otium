# By-value aggregate lowering

Otium keeps memory layout separate from call ABI classification. Structs and
embedded arrays become libffi struct descriptors. Each descriptor is prepared
and checked against the shared layout's size, alignment, and field offsets.
Chez uses private struct, union, and array ftypes through `(& ftype)`.

libffi has no union descriptor. The C backend constructs a synthetic struct
with the union's size, alignment, and register classes. This descriptor is only
used to marshal bytes. Field access still uses the original union layout.
No member is selected as the active union variant.

On arm64, a union whose scalar leaves all have the same float type and whose
size fits one to four elements is represented by those floating elements.
Other unions use integer elements of the union's alignment. This preserves
homogeneous floating aggregate classification, including a union with one
float member and a larger array of floats. The calling engine handles register
exhaustion, stack arguments, and indirect arguments/results.
See the [AAPCS64 composite and homogeneous aggregate rules](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
and [libffi's arm64 implementation](https://github.com/libffi/libffi/blob/master/src/aarch64/ffi.c).

On x86-64, the classifier merges the integer and floating classes of every
member at its byte offset. Integer class wins where members overlap. The
synthetic struct uses alignment-sized integer or floating elements, allowing
libffi to merge them into SysV eightbytes at the actual enclosing offset.
Keeping this resolution matters: a four-byte-aligned union can begin halfway
through an eightbyte inside a struct. Classifying the standalone union into
eightbytes too early can incorrectly turn an enclosing SSE eightbyte into an
integer eightbyte. Larger-than-16-byte unions use integer elements and are
passed in memory for this scalar-only type profile.
See [libffi's SysV classification and merging rules](https://github.com/libffi/libffi/blob/master/src/x86/ffi64.c).

This lowering excludes packed and empty aggregates, zero-length array fields,
bitfields, vectors, complex numbers, long double, and custom alignment. Those
types require additional ABI classes or layout rules. The two initial targets
use naturally aligned scalar members with alignments of 1, 2, 4, or 8 bytes.

The fixture suite covers mixed unions, homogeneous float/double unions on each
side of the four-element limit, nested structs and arrays, a four-byte-offset
union, small integer unions, register exhaustion, and full-width integer bits
transported through aggregate results. Compiler-computed sizes and offsets
check the Otium declarations independently. macOS arm64 passes on both runtimes.
The x86-64 lowering is implemented but Linux execution remains unverified;
there is no running Linux VM in the current development environment.
