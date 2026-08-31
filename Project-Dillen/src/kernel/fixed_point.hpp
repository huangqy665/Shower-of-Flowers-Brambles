#pragma once

#include <cstdint>

namespace dillen::kernel {

// Deterministic decimal arithmetic.
//
// `MechanismValueKind::Decimal` is stored as a double, and that stays true --
// this header does not change storage, only how arithmetic on decimals is
// carried out. Every arithmetic and aggregation step runs on scaled int64,
// never on the double itself, so a sum or a product is bit-identical on every
// platform without depending on floating-point flags, FMA contraction rules or
// the still-open cross-platform float question (memo section 4.6).
//
// Two scales, deliberately different:
//
//   Storage  10^2  -- the observable contract. Author writes 2 decimals, the
//                     save holds 2 decimals, the Query returns 2 decimals.
//   Internal 10^4  -- lives only inside one expression. Without it, chaining
//                     would quantise at every step and `0.07 * 0.07` would
//                     collapse to 0.00 rather than reaching the store as
//                     0.0049 -> 0.00. Quantisation happens once, at the store.
//
// The internal scale is 10^4 rather than the 10^6 first sketched, and the
// reason is portability, not precision. A product of two internally-scaled
// values carries scale^2, so the intermediate must satisfy
// |a * b| < 2^63 / scale^2:
//
//   scale 10^6  ->  |a * b| < 9.2e6      needs a 128-bit intermediate, which
//                                        means __int128 on GCC/Clang and
//                                        _mul128/_div128 on MSVC -- two code
//                                        paths for one contract
//   scale 10^4  ->  |a * b| < 9.2e10     fits int64 outright
//
// 10^4 keeps two digits of headroom below the storage quantum, so a chain of
// several operations accumulates rounding error around 1e-4 -- two orders of
// magnitude finer than the 1e-2 the result is quantised to anyway. Buying two
// more digits by taking on a second 128-bit code path would trade a real
// cross-platform hazard for precision nothing can observe.
//
// The internal scale is NOT a frozen contract: it never reaches a save, a
// Query or a Package. The storage scale is.
inline constexpr std::int64_t kDecimalStorageScale = 100;
inline constexpr std::int64_t kDecimalInternalScale = 10000;

enum class FixedPointStatus
{
    Ok,
    // Signed overflow is undefined behaviour, so it is checked before the
    // operation rather than detected after: once it has happened there is no
    // "deterministically wrong" left to report.
    Overflow,
    DivideByZero,
    NotFinite
};

struct FixedPointValue
{
    std::int64_t scaled = 0;
    FixedPointStatus status = FixedPointStatus::Ok;

    explicit operator bool() const noexcept
    {
        return status == FixedPointStatus::Ok;
    }
};

// double -> internal scale. Rejects NaN and infinity: they have no fixed-point
// image, and silently mapping them to zero would hide a real authoring error.
FixedPointValue DecimalToInternal(double value) noexcept;

// internal scale -> double, quantised to the storage scale. This is the one
// place quantisation happens.
FixedPointValue InternalToStorage(
    std::int64_t internalScaled,
    double& output
) noexcept;

// Integers travel through the same pipeline so that mixed-kind expressions
// have one set of rules; an integer is simply exact at any scale.
FixedPointValue IntegerToInternal(std::int64_t value) noexcept;
FixedPointValue InternalToInteger(
    std::int64_t internalScaled,
    std::int64_t& output
) noexcept;

// All four take and return internally-scaled values. Rounding, where a
// result is not exact, is half-away-from-zero -- chosen over truncation
// because truncation biases every accumulation toward zero, and an economy
// that accumulates for ten thousand ticks would visibly drift.
FixedPointValue FixedAdd(std::int64_t left, std::int64_t right) noexcept;
FixedPointValue FixedSubtract(std::int64_t left, std::int64_t right) noexcept;
FixedPointValue FixedMultiply(std::int64_t left, std::int64_t right) noexcept;
FixedPointValue FixedDivide(std::int64_t left, std::int64_t right) noexcept;

}
