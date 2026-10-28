// Vector networks: conversion from contours, stroke chaining, editing and planar faces.

#include "Types.h"

#include <include/core/SkPathBuilder.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>

namespace drav {

namespace {

SkPoint cubicAt(SkPoint p0, SkPoint c1, SkPoint c2, SkPoint p3, float t)
{
    const float u = 1 - t;
    return p0 * (u * u * u) + c1 * (3 * u * u * t) + c2 * (3 * u * t * t) + p3 * (t * t * t);
}

void appendSegment(SkPathBuilder& b, const VectorData& v, int s, bool forward)
{
    const NetSegment& sg = v.segments[s];
    const SkPoint pa = v.vertices[sg.a].p, pb = v.vertices[sg.b].p;
    const bool straight = sg.ca == pa && sg.cb == pb;
    if (forward) {
        if (straight)
            b.lineTo(pb);
        else
            b.cubicTo(sg.ca, sg.cb, pb);
    } else {
        if (straight)
            b.lineTo(pa);
        else
            b.cubicTo(sg.cb, sg.ca, pa);
    }
}

// Start vertex of a loop given in walking order.
int loopStart(const VectorData& v, const std::vector<int>& loop)
{
    const NetSegment& s0 = v.segments[loop[0]];
    if (loop.size() == 1)
        return s0.a;
    const NetSegment& s1 = v.segments[loop[1]];
    return (s0.b == s1.a || s0.b == s1.b) ? s0.a : s0.b;
}

} // namespace

SkPath VectorData::loopPath(const std::vector<int>& loop) const
{
    SkPathBuilder b;
    if (loop.empty())
        return b.detach();
    int at = loopStart(*this, loop);
    b.moveTo(vertices[at].p);
    for (int s : loop) {
        if (s < 0 || s >= int(segments.size()))
            continue;
        const bool forward = segments[s].a == at;
        appendSegment(b, *this, s, forward);
        at = forward ? segments[s].b : segments[s].a;
    }
    b.close();
    return b.detach();
}

SkPath VectorData::strokePath() const
{
    if (!isNetwork())
        return toPath();
    SkPathBuilder b;
    const int nv = int(vertices.size()), ns = int(segments.size());
    std::vector<std::vector<int>> adj(nv);
    for (int s = 0; s < ns; ++s) {
        adj[segments[s].a].push_back(s);
        if (segments[s].b != segments[s].a)
            adj[segments[s].b].push_back(s);
    }
    std::vector<bool> used(ns, false);
    // Walk chains through vertices that join exactly two segments, so strokes get proper joins.
    auto walk = [&](int v0, int s0) {
        b.moveTo(vertices[v0].p);
        int at = v0, s = s0;
        while (s >= 0) {
            used[s] = true;
            const bool forward = segments[s].a == at;
            appendSegment(b, *this, s, forward);
            at = forward ? segments[s].b : segments[s].a;
            if (at == v0 && adj[at].size() == 2) {
                b.close();
                return;
            }
            int next = -1;
            if (adj[at].size() == 2)
                for (int o : adj[at])
                    if (!used[o])
                        next = o;
            s = next;
        }
    };
    for (int v = 0; v < nv; ++v)
        if (adj[v].size() != 2)
            for (int s : adj[v])
                if (!used[s])
                    walk(v, s);
    for (int s = 0; s < ns; ++s) // remaining closed rings of simple vertices
        if (!used[s])
            walk(segments[s].a, s);
    for (int v = 0; v < nv; ++v) // lone vertices draw nothing but keep the geometry meaningful
        (void)v;
    return b.detach();
}

VectorData VectorData::asNetwork() const
{
    if (isNetwork())
        return *this;
    VectorData n;
    n.fill = fill;
    for (const Contour& c : contours) {
        const int base = int(n.vertices.size());
        const int count = int(c.pts.size());
        if (!count)
            continue;
        for (const Anchor& a : c.pts)
            n.vertices.push_back({a.p, a.mode});
        std::vector<int> loop;
        const int segs = c.closed ? count : count - 1;
        for (int i = 0; i < segs; ++i) {
            const Anchor& a = c.pts[i];
            const Anchor& z = c.pts[(i + 1) % count];
            loop.push_back(int(n.segments.size()));
            n.segments.push_back({base + i, base + (i + 1) % count, a.out, z.in});
        }
        if (!loop.empty())
            n.regions.push_back({{loop}}); // contours were filled, closed or not
    }
    return n;
}

std::vector<int> VectorData::segmentsAt(int vertex) const
{
    std::vector<int> out;
    for (int s = 0; s < int(segments.size()); ++s)
        if (segments[s].a == vertex || segments[s].b == vertex)
            out.push_back(s);
    return out;
}

int VectorData::splitSegment(int s, float t)
{
    const NetSegment sg = segments[s];
    // For each loop through s: does the walk traverse it from sg.a? (decided before changing s)
    std::vector<std::pair<std::vector<int>*, bool>> through;
    for (NetRegion& r : regions)
        for (auto& loop : r.loops)
            for (size_t i = 0; i < loop.size(); ++i)
                if (loop[i] == s) {
                    int at = loopStart(*this, loop);
                    for (size_t k = 0; k < i; ++k) {
                        const NetSegment& x = segments[loop[k]];
                        at = x.a == at ? x.b : x.a;
                    }
                    through.emplace_back(&loop, at == sg.a);
                    break;
                }
    const SkPoint p0 = vertices[sg.a].p, p3 = vertices[sg.b].p;
    const bool straight = sg.ca == p0 && sg.cb == p3;
    auto lerp = [](SkPoint a, SkPoint b, float u) { return a + (b - a) * u; };
    const SkPoint p01 = lerp(p0, sg.ca, t), p12 = lerp(sg.ca, sg.cb, t), p23 = lerp(sg.cb, p3, t);
    const SkPoint p012 = lerp(p01, p12, t), p123 = lerp(p12, p23, t), mid = lerp(p012, p123, t);
    const int v = int(vertices.size());
    vertices.push_back({mid, straight ? AnchorMode::Corner : AnchorMode::Asymmetric});
    segments[s] = {sg.a, v, straight ? p0 : p01, straight ? mid : p012};
    const int s2 = int(segments.size());
    segments.push_back({v, sg.b, straight ? mid : p123, straight ? p3 : p23});
    // s2 follows s when walking a -> b, precedes it when walking b -> a.
    for (auto& [loop, fromA] : through) {
        auto it = std::find(loop->begin(), loop->end(), s);
        loop->insert(fromA ? it + 1 : it, s2);
    }
    return v;
}

int VectorData::addSegment(int a, int b, SkPoint ca, SkPoint cb, bool fillLoops)
{
    // Shortest existing path from b back to a: with the new segment it forms a loop.
    std::vector<int> back;
    if (fillLoops && a != b) {
        std::vector<int> prevSeg(vertices.size(), -1), prevV(vertices.size(), -1);
        std::vector<bool> seen(vertices.size(), false);
        std::deque<int> q{b};
        seen[b] = true;
        while (!q.empty() && !seen[a]) {
            const int v = q.front();
            q.pop_front();
            for (int s : segmentsAt(v)) {
                const int o = segments[s].a == v ? segments[s].b : segments[s].a;
                if (!seen[o]) {
                    seen[o] = true;
                    prevSeg[o] = s;
                    prevV[o] = v;
                    q.push_back(o);
                }
            }
        }
        if (seen[a])
            for (int v = a; v != b; v = prevV[v])
                back.push_back(prevSeg[v]); // a ... b
    }
    const int s = int(segments.size());
    segments.push_back({a, b, ca, cb});
    if (!back.empty()) {
        std::vector<int> loop{s}; // a -> b, then b back to a
        loop.insert(loop.end(), back.rbegin(), back.rend());
        // An open path's implicit fill over the same segments becomes this closed loop.
        const std::set<int> rest(back.begin(), back.end());
        for (NetRegion& r : regions)
            for (auto& l : r.loops)
                if (std::set<int>(l.begin(), l.end()) == rest) {
                    l = loop;
                    return s;
                }
        regions.push_back({{loop}});
    }
    return s;
}

void VectorData::removeVertices(std::vector<int> gone)
{
    std::sort(gone.begin(), gone.end());
    gone.erase(std::unique(gone.begin(), gone.end()), gone.end());
    std::set<int> goneSet(gone.begin(), gone.end());
    std::vector<int> segMap(segments.size(), -1);
    std::vector<NetSegment> keptSegs;
    for (int s = 0; s < int(segments.size()); ++s)
        if (!goneSet.count(segments[s].a) && !goneSet.count(segments[s].b)) {
            segMap[s] = int(keptSegs.size());
            keptSegs.push_back(segments[s]);
        }
    std::vector<int> vMap(vertices.size(), -1);
    std::vector<NetVertex> keptV;
    for (int v = 0; v < int(vertices.size()); ++v)
        if (!goneSet.count(v)) {
            vMap[v] = int(keptV.size());
            keptV.push_back(vertices[v]);
        }
    for (NetSegment& sg : keptSegs) {
        sg.a = vMap[sg.a];
        sg.b = vMap[sg.b];
    }
    // Loops through a removed segment are dropped.
    std::vector<NetRegion> keptR;
    for (const NetRegion& r : regions) {
        NetRegion nr;
        for (const auto& loop : r.loops) {
            std::vector<int> l;
            bool ok = true;
            for (int s : loop)
                ok &= segMap[s] >= 0, l.push_back(segMap[s]);
            if (ok)
                nr.loops.push_back(l);
        }
        if (!nr.loops.empty())
            keptR.push_back(nr);
    }
    vertices = std::move(keptV);
    segments = std::move(keptSegs);
    regions = std::move(keptR);
}

std::vector<std::vector<int>> VectorData::faces() const
{
    // Half-edge face walk of the planar embedding (segments are assumed not to cross).
    const int ns = int(segments.size());
    struct Half {
        int seg;
        bool fwd;
        int from, to;
        float angle;
    };
    std::vector<Half> hs;
    for (int s = 0; s < ns; ++s) {
        const NetSegment& sg = segments[s];
        if (sg.a == sg.b)
            continue;
        const SkPoint pa = vertices[sg.a].p, pb = vertices[sg.b].p;
        const SkPoint da = (sg.ca != pa ? sg.ca : (sg.cb != pa ? sg.cb : pb)) - pa;
        const SkPoint db = (sg.cb != pb ? sg.cb : (sg.ca != pb ? sg.ca : pa)) - pb;
        hs.push_back({s, true, sg.a, sg.b, std::atan2(da.y(), da.x())});
        hs.push_back({s, false, sg.b, sg.a, std::atan2(db.y(), db.x())});
    }
    std::map<int, std::vector<int>> out; // vertex -> outgoing half-edges sorted by angle
    for (int h = 0; h < int(hs.size()); ++h)
        out[hs[h].from].push_back(h);
    for (auto& [v, list] : out)
        std::sort(list.begin(), list.end(), [&](int x, int y) { return hs[x].angle < hs[y].angle; });
    auto twin = [](int h) { return h ^ 1; };
    std::vector<bool> used(hs.size(), false);
    std::vector<std::vector<int>> faces;
    std::vector<float> areas;
    for (int h0 = 0; h0 < int(hs.size()); ++h0) {
        if (used[h0])
            continue;
        std::vector<int> loop;
        SkPathBuilder b;
        b.moveTo(vertices[hs[h0].from].p);
        int h = h0;
        for (int guard = 0; guard < int(hs.size()) + 1 && !used[h]; ++guard) {
            used[h] = true;
            loop.push_back(hs[h].seg);
            appendSegment(b, *this, hs[h].seg, hs[h].fwd);
            // Next: around the end vertex, the outgoing edge just before the twin (clockwise turn).
            const auto& list = out[hs[h].to];
            const int k = int(std::find(list.begin(), list.end(), twin(h)) - list.begin());
            h = list[(k - 1 + int(list.size())) % int(list.size())];
        }
        b.close();
        // Signed area from a flattened polygon.
        const SkPath path = b.detach();
        float area = 0;
        SkPath::Iter it(path, true);
        SkPoint pts[4], first{}, last{};
        bool start = true;
        for (SkPath::Verb verb; (verb = it.next(pts)) != SkPath::kDone_Verb;) {
            auto edge = [&](SkPoint p) {
                if (start) {
                    first = last = p;
                    start = false;
                    return;
                }
                area += last.x() * p.y() - p.x() * last.y();
                last = p;
            };
            if (verb == SkPath::kMove_Verb)
                edge(pts[0]);
            else if (verb == SkPath::kLine_Verb)
                edge(pts[1]);
            else if (verb == SkPath::kCubic_Verb)
                for (int i = 1; i <= 12; ++i)
                    edge(cubicAt(pts[0], pts[1], pts[2], pts[3], i / 12.f));
        }
        area += last.x() * first.y() - first.x() * last.y();
        faces.push_back(loop);
        areas.push_back(area / 2);
    }
    // Bounded faces turn one way; the outer boundary of each component turns the other.
    std::vector<std::vector<int>> bounded;
    for (size_t i = 0; i < faces.size(); ++i)
        if (areas[i] > 1e-3f)
            bounded.push_back(faces[i]);
    return bounded;
}

bool VectorData::toggleRegionAt(SkPoint p)
{
    // Smallest face containing the point.
    int best = -1;
    float bestArea = 0;
    const auto fs = faces();
    for (int i = 0; i < int(fs.size()); ++i) {
        const SkPath path = loopPath(fs[i]);
        if (!path.contains(p.x(), p.y()))
            continue;
        const SkRect b = path.computeTightBounds();
        const float a = b.width() * b.height();
        if (best < 0 || a < bestArea) {
            best = i;
            bestArea = a;
        }
    }
    if (best < 0)
        return false;
    std::set<int> face(fs[best].begin(), fs[best].end());
    for (auto it = regions.begin(); it != regions.end(); ++it)
        for (const auto& loop : it->loops)
            if (std::set<int>(loop.begin(), loop.end()) == face) {
                regions.erase(it);
                return true;
            }
    regions.push_back({{fs[best]}});
    return true;
}

} // namespace drav
