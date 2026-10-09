// The model the Fathom engine implements is defined by its roundings: where it
// rounds a product and then a sum, the two must not be fused into one
// operation. A source file includes this header before any other; the setting
// holds to the end of that file. No header includes it.
//
// The pragma governs what the compiler contracts of its own accord. A command
// line that asks for contraction in every file (-ffp-contract=fast, which
// -ffast-math and -Ofast imply) overrides it. The build therefore passes
// -ffp-contract=off for the files that include this header (CMakeLists.txt),
// and fast-math, which frees every rounding, is refused here.

#if defined(__FAST_MATH__)
 #error "The Fathom engine is defined by its roundings and cannot be built with fast-math."
#endif

#if defined(__clang__)
 #pragma clang fp contract(off)
#elif defined(__GNUC__)
 #pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
 #pragma fp_contract(off)
#endif
