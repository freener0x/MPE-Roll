#pragma once

#include <functional>
#include <vector>

namespace mperoll
{

struct CurvePoint
{
    double beat = 0.0;  // relative to the owning note's start, or to the clip start for zone lanes
    float value = 0.0f;

    bool operator== (const CurvePoint&) const = default;
};

/** A piecewise-linear automation curve with points sorted by time.

    Before the first point the curve holds the first value, after the last point it holds the
    last value. An empty curve means "no data": callers fall back to the dimension's default.
*/
class ExpressionCurve
{
public:
    ExpressionCurve() = default;
    ExpressionCurve (std::initializer_list<CurvePoint> initialPoints);

    bool isEmpty() const noexcept                         { return points.empty(); }
    size_t size() const noexcept                          { return points.size(); }
    const std::vector<CurvePoint>& getPoints() const noexcept { return points; }

    float valueAt (double beat, float fallback) const noexcept;

    /** Inserts a point, replacing any existing point at (almost) the same time. */
    void addPoint (double beat, float value);

    /** Fast path for recording: if the time is not earlier than the last point it is appended,
        otherwise it falls back to addPoint().
    */
    void append (double beat, float value);

    /** Removes all points with start <= beat <= end. */
    void removeRange (double start, double end);

    /** Replaces everything in [start, end] with the given (sorted) points. */
    void replaceRange (double start, double end, const std::vector<CurvePoint>& newPoints);

    /** Keeps only the section [start, end], adding interpolated boundary points so the shape is preserved. */
    void trimTo (double start, double end);

    void clear() noexcept                                 { points.clear(); }
    void shiftTime (double delta);
    void scaleTime (double factor);
    void transformValues (const std::function<float (float)>& fn);

    /** Ramer-Douglas-Peucker reduction using the vertical (value) error as the distance measure. */
    void simplify (float tolerance);

    /** True if every point is within tolerance of the given value. */
    bool isConstant (float value, float tolerance) const noexcept;

    float minValue (float fallback) const noexcept;
    float maxValue (float fallback) const noexcept;

    /** Time-weighted average over [start, end]. */
    float averageOver (double start, double end, float fallback) const noexcept;

    bool operator== (const ExpressionCurve&) const = default;

private:
    std::vector<CurvePoint> points;
};

} // namespace mperoll
