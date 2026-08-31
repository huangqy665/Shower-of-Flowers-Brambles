#include "fixed_point.hpp"

#include <cmath>
#include <limits>

namespace dillen::kernel {

namespace {

constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

FixedPointValue Fail(FixedPointStatus status) noexcept
{
    return {0, status};
}

// Round half away from zero on an exact numerator/denominator pair, without
// ever forming a quantity that could overflow: the quotient and remainder are
// computed first and only the remainder is doubled.
FixedPointValue DivideRounded(
    std::int64_t numerator,
    std::int64_t denominator
) noexcept
{
    if (denominator == 0)
    {
        return Fail(FixedPointStatus::DivideByZero);
    }
    // The single case where negation overflows.
    if (numerator == kMin && denominator == -1)
    {
        return Fail(FixedPointStatus::Overflow);
    }
    std::int64_t quotient = numerator / denominator;
    const std::int64_t remainder = numerator % denominator;
    if (remainder != 0)
    {
        // |remainder| * 2 >= |denominator| decides the round, and doubling the
        // remainder is safe only when it cannot overflow; compare by division
        // instead when it would.
        const bool roundAway = remainder > 0
            ? (remainder > kMax / 2
                ? true
                : remainder * 2 >= (denominator > 0
                    ? denominator
                    : -denominator))
            : (remainder < kMin / 2
                ? true
                : -remainder * 2 >= (denominator > 0
                    ? denominator
                    : -denominator));
        if (roundAway)
        {
            const bool negative = (numerator < 0) != (denominator < 0);
            const std::int64_t step = negative ? -1 : 1;
            if ((step > 0 && quotient > kMax - step)
                || (step < 0 && quotient < kMin - step))
            {
                return Fail(FixedPointStatus::Overflow);
            }
            quotient += step;
        }
    }
    return {quotient, FixedPointStatus::Ok};
}

}

FixedPointValue DecimalToInternal(double value) noexcept
{
    if (!std::isfinite(value))
    {
        return Fail(FixedPointStatus::NotFinite);
    }
    const double scaled =
        value * static_cast<double>(kDecimalInternalScale);
    // Bound before converting: casting an out-of-range double to int64 is
    // undefined behaviour, not a wrap.
    constexpr double kLimit = 9.0e18;
    if (!(scaled > -kLimit && scaled < kLimit))
    {
        return Fail(FixedPointStatus::Overflow);
    }
    // Half away from zero, matching every other rounding decision here.
    const double rounded = scaled < 0.0
        ? -std::floor(-scaled + 0.5)
        : std::floor(scaled + 0.5);
    return {static_cast<std::int64_t>(rounded), FixedPointStatus::Ok};
}

FixedPointValue InternalToStorage(
    std::int64_t internalScaled,
    double& output
) noexcept
{
    output = 0.0;
    constexpr std::int64_t kRatio =
        kDecimalInternalScale / kDecimalStorageScale;
    static_assert(
        kDecimalInternalScale % kDecimalStorageScale == 0,
        "the internal scale must be a whole multiple of the storage scale"
    );
    const FixedPointValue quantised = DivideRounded(internalScaled, kRatio);
    if (!quantised)
    {
        return quantised;
    }
    // Exact: |scaled| well under 2^53 for any value the engine can hold, and
    // the divisor is a power of ten that the double can represent.
    output = static_cast<double>(quantised.scaled)
        / static_cast<double>(kDecimalStorageScale);
    return {quantised.scaled, FixedPointStatus::Ok};
}

FixedPointValue IntegerToInternal(std::int64_t value) noexcept
{
    if (value > kMax / kDecimalInternalScale
        || value < kMin / kDecimalInternalScale)
    {
        return Fail(FixedPointStatus::Overflow);
    }
    return {value * kDecimalInternalScale, FixedPointStatus::Ok};
}

FixedPointValue InternalToInteger(
    std::int64_t internalScaled,
    std::int64_t& output
) noexcept
{
    output = 0;
    const FixedPointValue whole =
        DivideRounded(internalScaled, kDecimalInternalScale);
    if (whole)
    {
        output = whole.scaled;
    }
    return whole;
}

FixedPointValue FixedAdd(std::int64_t left, std::int64_t right) noexcept
{
    if ((right > 0 && left > kMax - right)
        || (right < 0 && left < kMin - right))
    {
        return Fail(FixedPointStatus::Overflow);
    }
    return {left + right, FixedPointStatus::Ok};
}

FixedPointValue FixedSubtract(std::int64_t left, std::int64_t right) noexcept
{
    if (right == kMin)
    {
        // -kMin is not representable; fold into the add path only when safe.
        return left < 0
            ? FixedPointValue{left - right, FixedPointStatus::Ok}
            : Fail(FixedPointStatus::Overflow);
    }
    return FixedAdd(left, -right);
}

FixedPointValue FixedMultiply(std::int64_t left, std::int64_t right) noexcept
{
    if (left != 0 && right != 0)
    {
        // Checked before multiplying, so no 128-bit intermediate and no
        // reliance on wrap-around that the standard does not grant.
        const std::int64_t absLeft = left < 0 ? -left : left;
        const std::int64_t absRight = right < 0 ? -right : right;
        if (left == kMin || right == kMin || absLeft > kMax / absRight)
        {
            return Fail(FixedPointStatus::Overflow);
        }
    }
    return DivideRounded(left * right, kDecimalInternalScale);
}

FixedPointValue FixedDivide(std::int64_t left, std::int64_t right) noexcept
{
    if (right == 0)
    {
        return Fail(FixedPointStatus::DivideByZero);
    }
    if (left != 0)
    {
        const std::int64_t absLeft = left < 0 ? -left : left;
        if (left == kMin || absLeft > kMax / kDecimalInternalScale)
        {
            return Fail(FixedPointStatus::Overflow);
        }
    }
    return DivideRounded(left * kDecimalInternalScale, right);
}

}
