# Bottlenecks

Known likely bottlenecks before optimization work:

- `buildLevel()` scans every peak/channel pair from source audio.
- `getPeakRange()` merges linearly across selected peaks.
- Shared-lock reader traffic may still contend with writer swap phases under
  heavy rebuild frequency.
