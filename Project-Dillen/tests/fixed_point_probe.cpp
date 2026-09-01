#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "fixed_point.hpp"

// Deterministic decimal arithmetic, checked before anything is built on it.
//
// The contract this pins: decimal arithmetic never touches a double except to
// enter and leave, so a result is a function of the inputs alone -- not of the
// platform, the optimiser, or the order the compiler happened to contract a
// multiply-add. Everything an aggregation will later rely on is here.

namespace
{
using namespace dillen::kernel;

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "fixed point: " << what << '\n';
        ++failures;
    }
}

// Round-trips a double through the internal scale and back to storage.
double Quantise(double value)
{
    const FixedPointValue internal = DecimalToInternal(value);
    Check(static_cast<bool>(internal), "quantise: to internal failed");
    double out = 0.0;
    const FixedPointValue stored = InternalToStorage(internal.scaled, out);
    Check(static_cast<bool>(stored), "quantise: to storage failed");
    return out;
}

void CheckRepresentation()
{
    // Every 2-decimal value is exact, which is the whole point: 0.01 has no
    // exact binary double, so accumulating it in floating point drifts.
    Check(Quantise(0.01) == 0.01, "0.01 did not survive the round trip");
    Check(Quantise(2.75) == 2.75, "2.75 did not survive the round trip");
    Check(Quantise(-3.5) == -3.5, "-3.5 did not survive the round trip");

    // Half away from zero, symmetric across the sign.
    Check(Quantise(0.005) == 0.01, "0.005 should round away from zero");
    Check(Quantise(-0.005) == -0.01, "-0.005 should round away from zero");
    Check(Quantise(0.004) == 0.00, "0.004 should round toward zero");

    // The documented cost of a 2-decimal storage scale.
    Check(Quantise(0.0049) == 0.00, "0.0049 quantises to zero at 2 decimals");
}

void CheckArithmetic()
{
    const auto value = [](double raw)
    {
        return DecimalToInternal(raw).scaled;
    };
    const auto store = [](std::int64_t internal)
    {
        double out = 0.0;
        InternalToStorage(internal, out);
        return out;
    };

    // The float classic: 0.1 + 0.2 != 0.3 in binary floating point.
    const FixedPointValue sum = FixedAdd(value(0.1), value(0.2));
    Check(static_cast<bool>(sum), "0.1 + 0.2 failed");
    Check(store(sum.scaled) == 0.3, "0.1 + 0.2 must be exactly 0.3");
    Check(0.1 + 0.2 != 0.3, "the double path really does disagree");

    // Intermediate precision: the product is below the storage quantum but
    // must not be destroyed before it reaches the store.
    const FixedPointValue product =
        FixedMultiply(value(0.07), value(0.07));
    Check(static_cast<bool>(product), "0.07 * 0.07 failed");
    Check(product.scaled == 49, "0.07 * 0.07 should hold 0.0049 internally");
    Check(store(product.scaled) == 0.0, "0.0049 stores as 0.00");

    const FixedPointValue scaled = FixedMultiply(value(1.5), value(4.0));
    Check(static_cast<bool>(scaled) && store(scaled.scaled) == 6.0,
        "1.5 * 4.0 should be 6.0");

    const FixedPointValue quotient = FixedDivide(value(1.0), value(4.0));
    Check(static_cast<bool>(quotient) && store(quotient.scaled) == 0.25,
        "1.0 / 4.0 should be 0.25");

    const FixedPointValue difference =
        FixedSubtract(value(0.3), value(0.1));
    Check(static_cast<bool>(difference) && store(difference.scaled) == 0.2,
        "0.3 - 0.1 should be exactly 0.2");
}

void CheckAssociativity()
{
    // Integer addition is associative; float addition is not. This is what
    // lets an aggregation be order independent instead of order constrained.
    const std::int64_t a = DecimalToInternal(0.1).scaled;
    const std::int64_t b = DecimalToInternal(0.2).scaled;
    const std::int64_t c = DecimalToInternal(0.3).scaled;
    const std::int64_t left =
        FixedAdd(FixedAdd(a, b).scaled, c).scaled;
    const std::int64_t right =
        FixedAdd(a, FixedAdd(b, c).scaled).scaled;
    Check(left == right, "fixed point addition must be associative");
}

void CheckIntegerBridge()
{
    std::int64_t out = 0;
    const FixedPointValue internal = IntegerToInternal(7);
    Check(static_cast<bool>(internal), "7 -> internal failed");
    Check(static_cast<bool>(InternalToInteger(internal.scaled, out))
        && out == 7, "7 must survive the integer round trip");

    const FixedPointValue mixed =
        FixedAdd(IntegerToInternal(2).scaled, DecimalToInternal(0.5).scaled);
    double stored = 0.0;
    InternalToStorage(mixed.scaled, stored);
    Check(stored == 2.5, "2 + 0.5 should be 2.5 across kinds");
}

void CheckRejection()
{
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

    // Overflow is rejected, never wrapped: signed overflow is undefined
    // behaviour, so "deterministically wrong" is not on the menu.
    Check(FixedAdd(kMax, 1).status == FixedPointStatus::Overflow,
        "max + 1 must be rejected");
    Check(FixedSubtract(kMin, 1).status == FixedPointStatus::Overflow,
        "min - 1 must be rejected");
    Check(FixedMultiply(kMax, kMax).status == FixedPointStatus::Overflow,
        "max * max must be rejected");
    Check(FixedDivide(1, 0).status == FixedPointStatus::DivideByZero,
        "division by zero must be rejected");
    Check(FixedMultiply(kMin, -1).status == FixedPointStatus::Overflow,
        "min * -1 must be rejected");

    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    Check(DecimalToInternal(infinity).status == FixedPointStatus::NotFinite,
        "infinity has no fixed-point image");
    Check(DecimalToInternal(nan).status == FixedPointStatus::NotFinite,
        "NaN has no fixed-point image");
    Check(DecimalToInternal(1e30).status == FixedPointStatus::Overflow,
        "1e30 exceeds the fixed-point range");
}

// Extreme-value vectors around int64's edges.
//
// Every one of these once reached a `-value` on an operand that had not been
// tested for kMin yet. Negating kMin has no representable result, so those
// were undefined behaviour on the way to the very check meant to prevent the
// overflow -- a sanitiser build would trap, and an optimiser is entitled to
// assume it cannot happen and delete the check.
//
// A rejection here is the correct answer; a wrong number or a crash is not.
void CheckExtremes()
{
    constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();

    // Subtract: `-right` used to be formed before anything looked at right.
    // 0 - kMin is kMax + 1, which is not representable.
    Check(FixedSubtract(0, kMin).status == FixedPointStatus::Overflow,
        "0 - kMin overflows");
    // These two ARE representable, and a blanket "reject every kMin operand"
    // fix would have turned correct answers into errors. kMin - kMin is 0 and
    // -1 - kMax is exactly kMin.
    FixedPointValue zero = FixedSubtract(kMin, kMin);
    Check(zero && zero.scaled == 0,
        "kMin - kMin is 0 and must not be rejected");
    FixedPointValue floor = FixedSubtract(-1, kMax);
    Check(floor && floor.scaled == kMin,
        "-1 - kMax is exactly kMin and must not be rejected");
    Check(static_cast<bool>(FixedSubtract(kMax, kMax)),
        "kMax - kMax is representable");
    Check(FixedSubtract(kMax, -1).status == FixedPointStatus::Overflow,
        "kMax - (-1) overflows");

    // Multiply: the absolute values were taken before the kMin test.
    Check(FixedMultiply(kMin, 1).status == FixedPointStatus::Overflow,
        "kMin * 1 must be rejected rather than negate kMin");
    Check(FixedMultiply(1, kMin).status == FixedPointStatus::Overflow,
        "1 * kMin must be rejected rather than negate kMin");
    Check(FixedMultiply(kMin, 0).status == FixedPointStatus::Ok,
        "anything * 0 is zero without touching the magnitudes");
    Check(FixedMultiply(kMax, kMax).status == FixedPointStatus::Overflow,
        "kMax * kMax overflows");

    // Divide: same shape, plus the denominator magnitude inside
    // DivideRounded, where `-denominator` was formed for the rounding test.
    Check(FixedDivide(kMin, 1).status == FixedPointStatus::Overflow,
        "kMin / 1 must be rejected rather than negate kMin");
    Check(FixedDivide(1, kMin).status == FixedPointStatus::Ok,
        "1 / kMin is representable and must not negate the denominator");
    // Rejected because FixedDivide pre-scales the numerator by the internal
    // scale, so a numerator anywhere near kMax cannot survive regardless of
    // the denominator. The point of the vector is that it is rejected rather
    // than negating kMin on the way.
    Check(FixedDivide(kMax, kMin).status == FixedPointStatus::Overflow,
        "kMax / kMin is rejected on the numerator, without negating kMin");
    Check(FixedDivide(1, 0).status == FixedPointStatus::DivideByZero,
        "division by zero is rejected");

    // Add: no negation, but the overflow guard has the same edges.
    Check(FixedAdd(kMax, 1).status == FixedPointStatus::Overflow,
        "kMax + 1 overflows");
    Check(FixedAdd(kMin, -1).status == FixedPointStatus::Overflow,
        "kMin - 1 underflows");
    Check(static_cast<bool>(FixedAdd(kMin, kMax)),
        "kMin + kMax is representable");

    // The integer bridge still has a scaled range, which is why an integer
    // field's delta must not be routed through it -- see the read-modify-write
    // in MechanismInstanceStore.
    Check(IntegerToInternal(kMax).status == FixedPointStatus::Overflow,
        "kMax has no scaled image");
    Check(IntegerToInternal(kMin).status == FixedPointStatus::Overflow,
        "kMin has no scaled image");
}

}

int main()
{
    CheckRepresentation();
    CheckArithmetic();
    CheckAssociativity();
    CheckIntegerBridge();
    CheckRejection();
    CheckExtremes();

    if (failures != 0)
    {
        std::cerr << "fixed point probe: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "fixed point probe: passed (storage scale "
              << kDecimalStorageScale << ", internal scale "
              << kDecimalInternalScale << ")\n";
    return 0;
}
