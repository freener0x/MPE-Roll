#include "ExpressionCurve.h"

#include <algorithm>
#include <cmath>

namespace mperoll
{

namespace
{
    constexpr double timeEpsilon = 1.0e-9;

    template <typename Points>
    auto lowerBound (Points& points, double beat)
    {
        return std::lower_bound (points.begin(), points.end(), beat,
                                 [] (const CurvePoint& p, double b) { return p.beat < b; });
    }
}

ExpressionCurve::ExpressionCurve (std::initializer_list<CurvePoint> initialPoints)
{
    for (const auto& p : initialPoints)
        addPoint (p.beat, p.value);
}

float ExpressionCurve::valueAt (double beat, float fallback) const noexcept
{
    if (points.empty())
        return fallback;

    if (beat <= points.front().beat)
        return points.front().value;

    if (beat >= points.back().beat)
        return points.back().value;

    auto next = std::upper_bound (points.begin(), points.end(), beat,
                                  [] (double b, const CurvePoint& p) { return b < p.beat; });
    auto prev = next - 1;

    const auto span = next->beat - prev->beat;

    if (span <= timeEpsilon)
        return next->value;

    const auto t = (beat - prev->beat) / span;
    return (float) (prev->value + (next->value - prev->value) * t);
}

void ExpressionCurve::addPoint (double beat, float value)
{
    auto it = lowerBound (points, beat - timeEpsilon);

    if (it != points.end() && std::abs (it->beat - beat) <= timeEpsilon)
    {
        it->value = value;
        return;
    }

    points.insert (it, { beat, value });
}

void ExpressionCurve::append (double beat, float value)
{
    if (points.empty() || beat > points.back().beat + timeEpsilon)
    {
        points.push_back ({ beat, value });
        return;
    }

    if (std::abs (beat - points.back().beat) <= timeEpsilon)
    {
        points.back().value = value;
        return;
    }

    addPoint (beat, value);
}

void ExpressionCurve::removeRange (double start, double end)
{
    if (end < start)
        std::swap (start, end);

    auto first = lowerBound (points, start - timeEpsilon);
    auto last = std::upper_bound (first, points.end(), end + timeEpsilon,
                                  [] (double b, const CurvePoint& p) { return b < p.beat; });
    points.erase (first, last);
}

void ExpressionCurve::replaceRange (double start, double end, const std::vector<CurvePoint>& newPoints)
{
    removeRange (start, end);

    for (const auto& p : newPoints)
        addPoint (p.beat, p.value);
}

void ExpressionCurve::trimTo (double start, double end)
{
    if (points.empty())
        return;

    if (end < start)
        std::swap (start, end);

    const auto startValue = valueAt (start, 0.0f);
    const auto endValue = valueAt (end, 0.0f);
    const bool needsStart = points.front().beat < start - timeEpsilon;
    const bool needsEnd = points.back().beat > end + timeEpsilon;

    std::vector<CurvePoint> kept;
    kept.reserve (points.size() + 2);

    if (needsStart)
        kept.push_back ({ start, startValue });

    for (const auto& p : points)
        if (p.beat >= start - timeEpsilon && p.beat <= end + timeEpsilon)
            kept.push_back (p);

    if (needsEnd && (kept.empty() || kept.back().beat < end - timeEpsilon))
        kept.push_back ({ end, endValue });

    points = std::move (kept);
}

void ExpressionCurve::shiftTime (double delta)
{
    for (auto& p : points)
        p.beat += delta;
}

void ExpressionCurve::scaleTime (double factor)
{
    for (auto& p : points)
        p.beat *= factor;
}

void ExpressionCurve::transformValues (const std::function<float (float)>& fn)
{
    for (auto& p : points)
        p.value = fn (p.value);
}

void ExpressionCurve::simplify (float tolerance)
{
    if (points.size() < 3)
    {
        if (points.size() == 2 && std::abs (points[0].value - points[1].value) <= tolerance * 0.5f)
            points.pop_back();

        return;
    }

    std::vector<bool> keep (points.size(), false);
    keep.front() = true;
    keep.back() = true;

    std::vector<std::pair<size_t, size_t>> stack;
    stack.emplace_back (0, points.size() - 1);

    while (! stack.empty())
    {
        const auto [first, last] = stack.back();
        stack.pop_back();

        if (last <= first + 1)
            continue;

        const auto& a = points[first];
        const auto& b = points[last];
        const auto span = b.beat - a.beat;

        float worst = -1.0f;
        size_t worstIndex = first;

        for (auto i = first + 1; i < last; ++i)
        {
            const auto t = span > timeEpsilon ? (points[i].beat - a.beat) / span : 0.0;
            const auto expected = (float) (a.value + (b.value - a.value) * t);
            const auto error = std::abs (points[i].value - expected);

            if (error > worst)
            {
                worst = error;
                worstIndex = i;
            }
        }

        if (worst > tolerance)
        {
            keep[worstIndex] = true;
            stack.emplace_back (first, worstIndex);
            stack.emplace_back (worstIndex, last);
        }
    }

    std::vector<CurvePoint> reduced;
    reduced.reserve (points.size());

    for (size_t i = 0; i < points.size(); ++i)
        if (keep[i])
            reduced.push_back (points[i]);

    // A flat curve only needs a single point.
    if (reduced.size() == 2 && std::abs (reduced[0].value - reduced[1].value) <= tolerance * 0.5f)
        reduced.pop_back();

    points = std::move (reduced);
}

bool ExpressionCurve::isConstant (float value, float tolerance) const noexcept
{
    return std::all_of (points.begin(), points.end(),
                        [&] (const CurvePoint& p) { return std::abs (p.value - value) <= tolerance; });
}

float ExpressionCurve::minValue (float fallback) const noexcept
{
    if (points.empty())
        return fallback;

    return std::min_element (points.begin(), points.end(),
                             [] (const CurvePoint& a, const CurvePoint& b) { return a.value < b.value; })->value;
}

float ExpressionCurve::maxValue (float fallback) const noexcept
{
    if (points.empty())
        return fallback;

    return std::max_element (points.begin(), points.end(),
                             [] (const CurvePoint& a, const CurvePoint& b) { return a.value < b.value; })->value;
}

float ExpressionCurve::averageOver (double start, double end, float fallback) const noexcept
{
    if (points.empty())
        return fallback;

    if (end - start <= timeEpsilon)
        return valueAt (start, fallback);

    double area = 0.0;
    double t0 = start;
    float v0 = valueAt (start, fallback);

    for (auto it = lowerBound (points, start + timeEpsilon); it != points.end() && it->beat < end; ++it)
    {
        area += (it->beat - t0) * (v0 + it->value) * 0.5;
        t0 = it->beat;
        v0 = it->value;
    }

    area += (end - t0) * (v0 + valueAt (end, fallback)) * 0.5;
    return (float) (area / (end - start));
}

} // namespace mperoll
