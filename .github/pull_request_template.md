## Decision citation

<!-- Required on every pull request. FD-12 — the strategic record in
     Aestra-Internals is the durable authority for scope, and work cites it.

     Objective   what this advances, e.g. "N3 — producer-loop scenario".
                 Leave as — only when Kind is corrective.
     Decision    the governing entry AND the commit it lives in: FD-04 @ a30696a.
                 The id alone is not enough. The record is a living file, so
                 "FD-04" does not say which FD-04 — the SHA is what makes a stale
                 citation visible in review.
     Kind        planned    advances an objective the record authorises
                 corrective fixes a defect in shipped or in-flight work
                 unparked   starts work a decision forbids or parks
     Reversal    unparked only. The NEW decision entry authorising it, by id.
                 A verbal approval is not an entry. If no entry exists, this
                 work is not ready to start — that is the point.

     Worked example:

       Objective:  N3 — producer-loop scenario in CI
       Decision:   FD-04 @ a30696a
       Kind:       planned
       Reversal:   —

     The check strips these comments before parsing, so the example above cannot
     answer on your behalf. -->

Objective:  <what this advances>
Decision:   <FD-NN @ sha>
Kind:       planned | corrective | unparked
Reversal:   —

## Summary

-

## Why

-

## Testing Performed

-

## Authority / invariant

<!-- Required for architectural or hardening changes; delete this section for
     anything else (a typo fix owes nobody an invariant).

     State the authority or invariant this change establishes, and what now
     prevents it from drifting again. See docs/technical/engineering-health.md.

     Two questions, two lines:
       What became the single source of truth?
       What now prevents that truth from drifting again?

     "A named constant, so 37 copies can't disagree" is an answer.
     "Cleaned up the geometry code" is not. -->

-

## Producer note

<!-- One sentence for the public changelog, in the voice of someone who
     makes beats — or "None". Default is None; most PRs are internal.

     Test: finish "now you can ..." or "X no longer ...".
     If neither sentence works, it's internal. Answer None.

       None                  refactor, test wiring, CI, dead code
       "Hitting pause no longer rewinds you."          <- ships
       "RMS detection with parameter smoothing"        <- fails the test

     Rough wording is fine. It gets polished at release time, when
     it's 25 lines to review instead of 400 commits to reconstruct. -->

None

## Docs Updated?

- [ ] Yes
- [ ] No
- [ ] Not needed

## Risk / Rollback Notes

-
