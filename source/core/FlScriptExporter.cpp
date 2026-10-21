#include "FlScriptExporter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace mperoll::FlScriptExporter
{

namespace
{
    constexpr int numColours = 16;
    constexpr float sustainThreshold = 0.5f;

    double sustainedEnd (const MpeNote& note, const MpeClip& clip)
    {
        const auto* pedal = clip.zone.find (LaneId::cc (64));

        if (pedal == nullptr || pedal->isEmpty() || pedal->valueAt (note.end(), 0.0f) < sustainThreshold)
            return note.end();

        for (const auto& p : pedal->getPoints())
            if (p.beat > note.end() && p.value < sustainThreshold)
                return p.beat;

        return std::max (note.end(), clip.contentEnd());
    }

    void addTimes (std::vector<double>& times, const ExpressionCurve& curve, double offset, double length)
    {
        for (const auto& p : curve.getPoints())
        {
            const auto t = p.beat - offset;

            if (t > 0.0 && t < length)
                times.push_back (t);
        }
    }

    /** Where a vibrato lane has any depth in a note, in beats from the note start. Empty (to <= from) if nowhere. */
    std::pair<double, double> vibratoStretch (const ExpressionCurve& depth, double offset, double length)
    {
        if (depth.isEmpty() || depth.maxValue (0.0f) <= 0.0f)
            return { 0.0, 0.0 };

        // A curve holds its first value before its first point and its last value after its last.
        const auto& first = depth.getPoints().front();
        const auto& last = depth.getPoints().back();
        return { first.value > 0.0f ? 0.0 : std::max (0.0, first.beat - offset),
                 last.value > 0.0f ? length : std::min (length, last.beat - offset) };
    }

    /** The shallowest the vibrato gets in a note where it has any depth, in semitones. 0 if it has none. */
    float shallowestVibrato (const ExpressionCurve& depth, double offset, double length)
    {
        auto shallowest = 0.0f;

        auto consider = [&shallowest] (float value)
        {
            if (value > 0.0f && (shallowest <= 0.0f || value < shallowest))
                shallowest = value;
        };

        consider (depth.valueAt (offset, 0.0f));
        consider (depth.valueAt (offset + length, 0.0f));

        for (const auto& p : depth.getPoints())
            if (p.beat > offset && p.beat < offset + length)
                consider (p.value);

        return shallowest * maxVibratoDepth;
    }

    /** Vibrato isn't piecewise linear: sample it often enough for the slides to follow its shape.
        The samples fall on a grid that includes every peak and trough, a quarter and three quarters
        of the way through each cycle. With two a cycle, they are just the peaks and troughs.
    */
    void addVibratoTimes (std::vector<double>& times, const ExpressionCurve& depth, double offset, double length,
                          double cycleBeats, int perCycle)
    {
        const auto [from, to] = vibratoStretch (depth, offset, length);

        if (to <= from || cycleBeats <= 0.0 || perCycle <= 0)
            return;

        // The phase is counted from the clip start for zone lanes (offset is the note start), as it's played.
        auto index = std::ceil (((offset + from) / cycleBeats - 0.25) * perCycle);

        for (;; ++index)
        {
            const auto t = (index / perCycle + 0.25) * cycleBeats - offset;

            if (t >= to || times.size() >= 100'000)
                break;

            if (t > 0.0)
                times.push_back (t);
        }
    }

    template <typename Fn>
    ExpressionCurve sampleCurve (std::vector<double> times, Fn&& valueAt)
    {
        std::sort (times.begin(), times.end());
        times.erase (std::unique (times.begin(), times.end()), times.end());

        ExpressionCurve curve;

        for (auto t : times)
            curve.append (t, valueAt (t));

        return curve;
    }

    std::vector<double> breakpoints (const ExpressionCurve& pitch, const ExpressionCurve& pressure,
                                     const ExpressionCurve& timbre, double length, double minGap)
    {
        std::vector<double> times;

        for (const auto* curve : { &pitch, &pressure, &timbre })
            for (const auto& p : curve->getPoints())
                if (p.beat > 0.0 && p.beat <= length)
                    times.push_back (p.beat);

        std::sort (times.begin(), times.end());

        std::vector<double> merged;

        for (auto t : times)
        {
            if (! merged.empty() && t - merged.back() < minGap)
                merged.back() = t;
            else if (merged.empty() && t < minGap)
                merged.push_back (std::min (minGap, length));
            else
                merged.push_back (t);
        }

        merged.erase (std::unique (merged.begin(), merged.end()), merged.end());
        return merged;
    }

    FlNote convertNote (const MpeNote& note, const MpeClip& clip, const Options& options)
    {
        FlNote fl;
        fl.start = note.start;
        fl.key = note.key;
        fl.strike = note.strike;
        fl.lift = note.lift;
        fl.length = std::max (1.0e-3, (options.applySustainPedal ? sustainedEnd (note, clip) : note.end()) - note.start);

        const auto length = fl.length;
        const auto& lanes = note.lanes;

        // Pitch: the note's own glide plus (optionally) the zone-wide bend. Both are piecewise linear,
        // so sampling at the union of their breakpoints is exact. Vibrato is sampled densely on top.
        const auto secondsPerBeat = 60.0 / std::max (1.0, options.tempoBpm);
        const auto cycleBeats = 1.0 / (clip.vibratoRate * secondsPerBeat);

        auto pitchTimes = [&] (int vibratoPerCycle)
        {
            std::vector<double> times { 0.0, length };
            addTimes (times, lanes.pitch, 0.0, length);
            addVibratoTimes (times, lanes.vibrato, 0.0, length, cycleBeats, vibratoPerCycle);

            if (options.bakeZonePitch)
            {
                addTimes (times, clip.zone.pitch, note.start, length);
                addVibratoTimes (times, clip.zone.vibrato, note.start, length, cycleBeats, vibratoPerCycle);
            }

            return times;
        };

        // How much vibrato the note has: the slides need one up to each peak and one down to each trough.
        auto vibratoCycles = 0.0;
        auto shallowest = 0.0f;

        auto addVibrato = [&] (const ExpressionCurve& depth, double offset)
        {
            const auto [from, to] = vibratoStretch (depth, offset, length);

            if (to <= from)
                return;

            vibratoCycles += (to - from) / cycleBeats;
            const auto depthHere = shallowestVibrato (depth, offset, length);

            if (depthHere > 0.0f && (shallowest <= 0.0f || depthHere < shallowest))
                shallowest = depthHere;
        };

        addVibrato (lanes.vibrato, 0.0);

        if (options.bakeZonePitch)
            addVibrato (clip.zone.vibrato, note.start);

        const bool hasVibrato = vibratoCycles > 0.0;

        // At the end the vibrato rests, as it does when MPE Roll plays the note, so the release rings at
        // the glide's pitch rather than wherever the last swing stopped.
        auto pitchAt = [&] (double t)
        {
            const bool atEnd = t >= length;
            auto value = lanes.pitch.valueAt (t, 0.0f);

            if (! atEnd)
                value += vibratoOffset (lanes.vibrato.valueAt (t, 0.0f), t / cycleBeats);

            if (options.bakeZonePitch)
            {
                value += clip.zone.pitch.valueAt (note.start + t, 0.0f);

                if (! atEnd)
                    value += vibratoOffset (clip.zone.vibrato.valueAt (note.start + t, 0.0f), (note.start + t) / cycleBeats);
            }

            return value;
        };

        auto rawPitch = sampleCurve (pitchTimes (8), pitchAt);

        std::vector<double> pressureTimes { 0.0, length };
        addTimes (pressureTimes, lanes.pressure, 0.0, length);
        const auto rawPressure = sampleCurve (pressureTimes, [&] (double t) { return lanes.pressure.valueAt (t, 0.0f); });

        std::vector<double> timbreTimes { 0.0, length };
        addTimes (timbreTimes, lanes.timbre, 0.0, length);
        const auto rawTimbre = sampleCurve (timbreTimes, [&] (double t) { return lanes.timbre.valueAt (t, 0.5f); });

        // Vibrato gets a slide up and a slide down each cycle on top of the usual budget, up to a limit
        // that keeps a very long note from flooding the piano roll.
        auto budget = options.maxSlidesPerNote;

        if (hasVibrato && budget > 0)
            budget = std::max (budget, std::min (options.maxSlidesPerVibratoNote,
                                                 budget + 2 * (int) std::ceil (std::min (vibratoCycles, 1.0e6))));

        auto glideTolerance = options.glideTolerance;
        auto rampTolerance = options.rampTolerance;
        ExpressionCurve pitch, pressure, timbre;
        std::vector<double> times;

        auto fitsBudget = [&]
        {
            pitch = rawPitch;
            pressure = rawPressure;
            timbre = rawTimbre;
            pitch.simplify (glideTolerance);
            pressure.simplify (rampTolerance);
            timbre.simplify (rampTolerance);

            times = breakpoints (pitch, pressure, timbre, length, options.minSlideLength);
            return budget <= 0 || (int) times.size() <= budget;
        };

        // Vibrato that doesn't fit sampled finely gives up the points between its peaks and troughs.
        if (hasVibrato && ! fitsBudget())
            rawPitch = sampleCurve (pitchTimes (2), pitchAt);

        // Relax the tolerances a little to fit the slide budget, then thin out evenly. The glide tolerance
        // stays under half the vibrato's depth, or the vibrato would be flattened away entirely.
        const auto glideLimit = shallowest > 0.0f ? std::max (glideTolerance, shallowest * 0.5f)
                                                  : std::numeric_limits<float>::max();

        for (int attempt = 0; attempt < 4; ++attempt)
        {
            if (fitsBudget())
                break;

            glideTolerance = std::min (glideTolerance * 2.0f, glideLimit);
            rampTolerance *= 2.0f;
        }

        // Still too many: thin out evenly. With vibrato, whole peak-and-trough pairs go, so what's left
        // still swings both ways at its depth instead of turning into slow swings or none.
        if (budget > 0 && (int) times.size() > budget)
        {
            std::vector<double> decimated;
            const auto groupSize = hasVibrato ? 2 : 1;
            const auto numGroups = (times.size() + (size_t) groupSize - 1) / (size_t) groupSize;
            const auto groupsToKeep = (size_t) std::max (1, budget / groupSize);
            const auto step = (double) numGroups / (double) groupsToKeep;

            for (size_t i = 1; i <= groupsToKeep; ++i)
            {
                const auto group = (size_t) std::min ((double) numGroups - 1.0, std::ceil ((double) i * step) - 1.0);

                for (auto j = group * (size_t) groupSize; j < std::min (times.size(), (group + 1) * (size_t) groupSize); ++j)
                    decimated.push_back (times[j]);
            }

            decimated.erase (std::unique (decimated.begin(), decimated.end()), decimated.end());
            times = std::move (decimated);
        }

        fl.initialPitch = pitch.valueAt (0.0, 0.0f);
        fl.initialPressure = pressure.valueAt (0.0, 0.0f);
        fl.initialTimbre = timbre.valueAt (0.0, 0.5f);
        fl.averagePressure = lanes.pressure.averageOver (0.0, length, 0.0f);
        fl.peakPressure = lanes.pressure.maxValue (-1.0f);
        fl.averageTimbre = lanes.timbre.averageOver (0.0, length, 0.5f);

        if (const auto* pan = lanes.find (LaneId::cc (10)); pan != nullptr && ! pan->isEmpty())
            fl.pan = pan->averageOver (0.0, length, 0.5f);

        double previousTime = 0.0;
        float previousPitch = fl.initialPitch;
        float previousPressure = fl.initialPressure;
        float previousTimbre = fl.initialTimbre;

        for (auto t : times)
        {
            if (t - previousTime <= 0.0)
                continue;

            Slide s;
            s.offset = previousTime;
            s.length = t - previousTime;
            s.pitch = pitch.valueAt (t, 0.0f);
            s.pressure = pressure.valueAt (t, 0.0f);
            s.timbre = timbre.valueAt (t, 0.5f);
            s.pitchMoves = std::abs (s.pitch - previousPitch) > glideTolerance * 0.5f;

            const bool modsMove = std::abs (s.pressure - previousPressure) > rampTolerance * 0.5f
                               || std::abs (s.timbre - previousTimbre) > rampTolerance * 0.5f;

            if (s.pitchMoves || modsMove)
                fl.slides.push_back (s);

            previousTime = t;
            previousPitch = s.pitch;
            previousPressure = s.pressure;
            previousTimbre = s.timbre;
        }

        return fl;
    }

    juce::String num (double value)
    {
        if (! std::isfinite (value))
            value = 0.0;

        char buffer[32];
        std::snprintf (buffer, sizeof (buffer), "%.6g", value);
        juce::String s (buffer);

        // Python needs a float literal to look like one; "%g" may print integers.
        if (! s.containsAnyOf (".eEn"))
            s << ".0";

        return s;
    }

    juce::String pyString (const juce::String& text)
    {
        return "\"" + text.replace ("\\", "\\\\").replace ("\"", "\\\"").removeCharacters ("\r\n") + "\"";
    }

    juce::String buildScript (const Result& result, const Options& options)
    {
        juce::String s;
        const auto title = options.title.isNotEmpty() ? options.title : juce::String ("MPE Roll clip");

        s << "# Generated by MPE Roll. Run it from FL Studio's piano roll Scripts menu.\n"
          << "# It rebuilds an MPE clip with FL Studio's own per-note tools so native plugins can play it.\n"
          << "import flpianoroll as flp\n\n"
          << "TITLE = " << pyString (title) << "\n\n"
          << "# FL's note fine pitch ('pitchofs') is in tenths of a semitone (-120..120).\n"
          << "# If glides land at the wrong pitch in your FL version, try 100 (cents).\n"
          << "PITCHOFS_PER_SEMITONE = 10\n\n"
          << "# (start, length, key, velocity, release, pan, initial pitch, pressure initial/average/peak,\n"
          << "#  timbre initial/average, colour, slides). Times in beats, pitch in semitones from the key.\n"
          << "# Peak pressure is -1 for a note without pressure.\n"
          << "# Each slide: (offset, length, pitch, pressure, timbre, pitch_moves).\n"
          << "NOTES = [\n";

        for (const auto& n : result.notes)
        {
            s << "    (" << num (n.start) << ", " << num (n.length) << ", " << n.key << ", "
              << num (n.strike) << ", " << num (n.lift) << ", " << num (n.pan) << ", "
              << num (n.initialPitch) << ", " << num (n.initialPressure) << ", " << num (n.averagePressure) << ", "
              << num (n.peakPressure) << ", " << num (n.initialTimbre) << ", " << num (n.averageTimbre) << ", "
              << n.colour << ", [";

            for (size_t i = 0; i < n.slides.size(); ++i)
            {
                const auto& sl = n.slides[i];
                s << (i > 0 ? ", " : "") << "(" << num (sl.offset) << ", " << num (sl.length) << ", "
                  << num (sl.pitch) << ", " << num (sl.pressure) << ", " << num (sl.timbre) << ", "
                  << (sl.pitchMoves ? "True" : "False") << ")";
            }

            s << "]),\n";
        }

        s << "]\n\n";

        s << R"PY(PRESSURE_TARGETS = "Mod X,Mod Y,Velocity,Off"
TIMBRE_TARGETS = "Mod Y,Mod X,Off"


def createDialog():
    form = flp.ScriptDialog("MPE Roll: " + TITLE,
                            "Rebuilds " + str(len(NOTES)) + " MPE notes with FL's per-note tools.\r\n"
                            "Pitch glides become slide notes. Every overlapping note gets its own colour,\r\n"
                            "because slide notes only bend notes of the same colour.\r\n"
                            "Pressure and timbre go to Mod X / Mod Y (map them in your plugin's per-note modulation).")
    form.AddInputCombo("Existing notes", "Replace,Keep", 0)
    form.AddInputCombo("Pitch glides", "Slide notes,Starting pitch only,Off", 0)
    form.AddInputCombo("Pressure", PRESSURE_TARGETS, 0)
    form.AddInputCombo("Timbre", TIMBRE_TARGETS, 0)
    form.AddInputCombo("Mod ramps on slide notes", "Yes,No", 0)
    form.AddInputCombo("Group slides with their note", "Yes,No", 0)
    return form


def _ticks(beats):
    return int(round(beats * flp.score.PPQ))


def _clamp(value, low, high):
    return max(low, min(high, value))


def _split_pitch(key, semitones):
    total = key + semitones
    number = int(_clamp(round(total), 0, 127))
    limit = 12 * PITCHOFS_PER_SEMITONE
    fine = int(_clamp(round((total - number) * PITCHOFS_PER_SEMITONE), -limit, limit))
    return number, fine


def _apply_mods(note, pressure, timbre, pressure_mode, timbre_mode):
    if pressure_mode == 0:
        note.fcut = _clamp(pressure, 0.0, 1.0)
    elif pressure_mode == 1:
        note.fres = _clamp(pressure, 0.0, 1.0)
    if timbre_mode == 0:
        note.fres = _clamp(timbre, 0.0, 1.0)
    elif timbre_mode == 1:
        note.fcut = _clamp(timbre, 0.0, 1.0)


def apply(form):
    replace = form.GetInputValue("Existing notes") == 0
    glide_mode = form.GetInputValue("Pitch glides")
    pressure_mode = form.GetInputValue("Pressure")
    timbre_mode = form.GetInputValue("Timbre")
    ramps = form.GetInputValue("Mod ramps on slide notes") == 0
    grouping = form.GetInputValue("Group slides with their note") == 0
    uses_mods = pressure_mode in (0, 1) or timbre_mode in (0, 1)

    if replace:
        flp.score.clear()

    group = 0
    for (start, length, key, velocity, release, pan, pitch0, pr0, pr_avg, pr_peak,
         tb0, tb_avg, colour, slides) in NOTES:
        wanted = []
        if glide_mode == 0:
            for slide in slides:
                if slide[5] or (ramps and uses_mods):
                    wanted.append(slide)

        note = flp.Note()
        number, fine = _split_pitch(key, pitch0 if glide_mode != 2 else 0.0)
        note.number = number
        note.pitchofs = fine
        note.time = _ticks(start)
        note.length = max(1, _ticks(start + length) - note.time)
        note.velocity = _clamp(pr_peak if pressure_mode == 2 and pr_peak >= 0 else velocity, 0.0, 1.0)
        note.release = _clamp(release, 0.0, 1.0)
        note.pan = _clamp(pan, 0.0, 1.0)
        note.color = colour
        if ramps and wanted:
            _apply_mods(note, pr0, tb0, pressure_mode, timbre_mode)
        else:
            _apply_mods(note, pr_avg, tb_avg, pressure_mode, timbre_mode)

        if grouping and wanted:
            group += 1
            note.group = group
        flp.score.addNote(note)

        for (offset, slide_length, pitch, pressure, timbre, moves) in wanted:
            slide = flp.Note()
            number, fine = _split_pitch(key, pitch)
            slide.number = number
            slide.pitchofs = fine
            slide.time = _ticks(start + offset)
            slide.length = max(1, _ticks(start + offset + slide_length) - slide.time)
            slide.slide = True
            slide.velocity = note.velocity
            slide.release = note.release
            slide.pan = note.pan
            slide.color = colour
            _apply_mods(slide, pressure, timbre, pressure_mode, timbre_mode)
            if grouping:
                slide.group = group
            flp.score.addNote(slide)
)PY";

        return s;
    }
}

Result generate (const MpeClip& clip, const Options& options)
{
    Result result;
    result.notes.reserve (clip.notes.size());

    for (const auto& note : clip.notes)
        result.notes.push_back (convertNote (note, clip, options));

    std::stable_sort (result.notes.begin(), result.notes.end(),
                      [] (const FlNote& a, const FlNote& b) { return a.start < b.start; });

    // Give overlapping notes different colours so each note's slides only bend that note.
    std::array<double, numColours> freeFrom;
    freeFrom.fill (-1.0e12);

    for (auto& n : result.notes)
    {
        int colour = -1;

        for (int c = 0; c < numColours; ++c)
        {
            if (freeFrom[(size_t) c] <= n.start + 1.0e-9)
            {
                colour = c;
                break;
            }
        }

        if (colour < 0)
        {
            colour = (int) (std::min_element (freeFrom.begin(), freeFrom.end()) - freeFrom.begin());
            ++result.numColourConflicts;
        }

        n.colour = colour;
        freeFrom[(size_t) colour] = std::max (freeFrom[(size_t) colour], n.start + n.length);
        result.numSlides += (int) n.slides.size();
    }

    result.script = buildScript (result, options);
    return result;
}

juce::File defaultScriptFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Image-Line")
               .getChildFile ("FL Studio")
               .getChildFile ("Settings")
               .getChildFile ("Piano roll scripts");
}

} // namespace mperoll::FlScriptExporter
