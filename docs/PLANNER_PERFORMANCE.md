# Planner performance and follow-up analysis

The planner now bounds global connection searches and resets only the BFS parent
entries it touched. It preserves the existing sample selection, tie-breaking,
route, and serialized output. Resolution, edge thresholds, preview rendering and
image-processing parallelism are unchanged.

On the 2048-pixel portrait used during development, planning time changed as follows:

| Thresholds / blur | Original planner | Integrated planner |
| --- | ---: | ---: |
| 50 / 150 / 5 | 38.99 s | 0.38 s |
| 30 / 90 / 5 | 148.74 s | 1.13 s |

Measurements used a Release build with GCC 16.2.1 on a Ryzen AI 9 365 with 20
logical CPUs. Integrated times are medians of three runs. The earlier original
measurements used three default runs and one detailed run. The machine was not
isolated or frequency-locked; these are local comparisons, not latency guarantees.
They exclude image decoding/resizing, output rendering, transport and browser work.
THR files, full PNGs and thumbnails matched the original byte for byte for both
settings.

The search starts with a real connection from an eligible unvisited sample to
the current path position. This supplies a distance bound before any grid scan.
The bound shrinks whenever a better connection is found, and grid cells whose
minimum distance is strictly worse are skipped. Equal-distance candidates remain
eligible for the original coordinate ordering. Adjacent pairs can be resolved
directly: on the integer lattice, their squared distances of 1 or 2 beat all
nonadjacent pairs. Longer searches enumerate ring perimeters, avoiding repeated
iteration over the ring interiors. The reduced search runs sequentially, avoiding
fresh asynchronous workers at each restart.

BFS records every enqueued parent entry and resets that list on the next search.
This includes the unfinished frontier when a target is reached early. DSU path
halving shortens parent chains without changing component roots or their order.

Regression tests capture original route choices around equal distances, grid
boundaries and component sizes 20/21/100/101, plus a fixed digest of 80 deterministic
point-cloud routes. An additional development comparison checked 160 randomized
inputs against the original implementation. Release and AddressSanitizer /
UndefinedBehaviorSanitizer CTest suites pass.

## Further improvements identified in the parallel review

These are proposals, not implemented or benchmarked speedups.

1. **Maintain adjacent candidates incrementally.** The current planner still
   rebuilds eligible samples and checks their neighbors at every restart. Track
   regular-sample eligibility per component and advance a fallback cursor when
   those samples are exhausted. Maintain adjacent unvisited/drawn pairs in a heap
   ordered by the existing distance/coordinate comparator. Each newly drawn pixel
   or newly eligible fallback checks eight neighbors; discard stale pairs when the
   unvisited point becomes visited. This should preserve output, provided sample
   transitions, pair insertion and tie ordering remain exact. General nearest-pair
   caching needs more care: cached distances are upper bounds as the drawn path
   grows, so a naive lazy heap can miss a newly shorter connection elsewhere.

2. **Choose the circular framing before planning.** In the portrait edge masks,
   41.42% of default edge points and 45.71% of detailed edge points lie outside the
   inscribed circle; a 3072-pixel experiment reaches 47.75%. Those points are planned
   in image space and then clamped to the rim by the THR conversion. An explicit
   circular crop removes that detail, while fitting the entire rectangular image
   within the circle preserves composition at a smaller scale. Compare these as
   distinct user choices rather than silently changing the current mapping.

3. **Return along existing tracks.** THR generation appends a radial return to the
   center. Returning through the drawn network could remove this new straight
   stroke, at the cost of additional travel and bytes. Strict retracing must track
   actual drawn segments: adjacent occupied pixels do not always mean their
   connecting segment was previously drawn.

4. **Improve gap bridging and contour routing.** The edge detector currently treats
   degree-two pixels as endpoints, so ordinary contour interiors participate in
   bridging. Compare true-terminal detection and direction-aware gap closure
   against the pre-bridge edge map. For a larger routing redesign, trace contours,
   cluster junctions, and collapse chains into graph edges retaining their full
   polylines. Select connections using both new-stroke cost and retracing cost.
   Treating every eight-neighbor adjacency as a required graph edge would instead
   create artificial triangles and overdraw.

For exact optimizations, retain byte-identical checks including sample exhaustion,
bucket boundaries, equal-distance ties and BFS exits with queued nodes. For route
quality changes, measure contour coverage, added stroke length, retraced distance,
THR size and planning time, and inspect previews generated from serialized polar
coordinates. Higher resolution by itself does not address connector artifacts or
the table's 8 MiB THR limit.
