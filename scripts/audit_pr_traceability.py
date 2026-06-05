#!/usr/bin/env python3
"""
Phase 1c — PR Traceability Layer

Analyzes the causal graph between issues, PRs, and changed files.

Metrics:
  1. PR linkage rate: % of merged PRs with issue reference
  2. Orphaned merges: PRs merged without issue linkage, by subsystem
  3. Issue closure traceability: % of closed issues with linked PR
  4. Linkage trend: how linkage rate changes over time
  5. Subsystem exposure: which areas receive untracked changes

Scope: last 100 merged PRs, last 90 days of closed issues.
Fully deterministic. No semantic inference.

Usage:
  python3 scripts/audit_pr_traceability.py
  python3 scripts/audit_pr_traceability.py --pr-limit 50
"""

import json
import os
import re
import subprocess
import sys
import time
from collections import defaultdict, Counter
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CACHE_DIR = REPO_ROOT / ".aestra" / "audit"
CACHE_DIR.mkdir(parents=True, exist_ok=True)
MERGED_PRS_CACHE = CACHE_DIR / "merged_prs.json"
CLOSED_ISSUES_CACHE = CACHE_DIR / "closed_issues.json"
PR_FILES_CACHE = CACHE_DIR / "pr_files.json"

FILE_TO_COMP = [
    ("AestraAudio/src/Core/", ["comp: audio-engine", "comp: serialization"]),
    ("AestraAudio/include/Core/", ["comp: audio-engine", "comp: serialization"]),
    ("AestraAudio/src/DSP/", ["comp: dsp"]),
    ("AestraAudio/include/DSP/", ["comp: dsp"]),
    ("AestraAudio/src/Plugin/", ["comp: audio-engine"]),
    ("AestraAudio/include/Plugin/", ["comp: audio-engine"]),
    ("AestraAudio/src/Routing/", ["comp: routing"]),
    ("AestraAudio/include/Routing/", ["comp: routing"]),
    ("AestraAudio/src/Playback/", ["comp: audio-engine"]),
    ("AestraAudio/src/IO/", ["comp: audio-engine"]),
    ("AestraAudio/src/Headless/", ["comp: audio-engine"]),
    ("AestraAudio/src/Models/", ["comp: audio-engine"]),
    ("AestraAudio/src/Mixer/", ["comp: audio-engine"]),
    ("AestraUI/", ["comp: ui"]),
    ("Source/", ["comp: ui"]),
    ("AestraCore/", ["comp: audio-engine"]),
    ("AestraPlat/", ["comp: platform"]),
    ("Tests/", ["comp: ci", "comp: audio-engine"]),
    ("Testing/", ["comp: ci", "comp: audio-engine"]),
    (".github/workflows/", ["comp: ci"]),
    (".github/", ["comp: ci"]),
    ("scripts/", ["comp: ci"]),
    ("cmake/", ["comp: ci"]),
    ("CMakeLists.txt", ["comp: ci"]),
    ("CMakePresets.json", ["comp: ci"]),
    ("docs/", ["comp: ci"]),
    ("labs/", ["comp: dsp"]),
    ("installer/", ["comp: ci"]),
    ("workers/", ["comp: ci"]),
]


def comp_for_file(filepath):
    """Map a file path to likely comp: labels."""
    for prefix, comps in FILE_TO_COMP:
        if filepath.startswith(prefix) or filepath == prefix:
            return comps
    return ["unknown"]


def run_gh(*args, timeout=30):
    try:
        result = subprocess.run(
            ["gh", *args],
            capture_output=True, text=True, timeout=timeout,
        )
        if result.returncode != 0:
            return None
        return result.stdout
    except subprocess.TimeoutExpired:
        return None


def has_fix_signal(text, issue_number=None):
    """Check if text contains an explicit issue fix reference."""
    if not text:
        return False, None
    if issue_number:
        patterns = [
            rf'(?:fix(?:es|ed)?|close[sd]?|resolve[sd]?)\s+#{issue_number}\b',
            rf'#{issue_number}\s+(?:fix|close|resolve)',
        ]
    else:
        patterns = [
            r'(?:fix(?:es|ed)?|close[sd]?|resolve[sd]?)\s+#(\d+)\b',
        ]
    text_lower = text.lower()
    for p in patterns:
        m = re.search(p, text_lower)
        if m:
            groups = m.groups()
            ref = int(groups[0]) if groups else issue_number
            return True, ref
    return False, None


def extract_all_issue_refs(text):
    """Extract all issue references from text with their context."""
    if not text:
        return []
    refs = []
    text_lower = text.lower()
    # Fixes #N pattern
    for m in re.finditer(r'(?:fix(?:es|ed)?|close[sd]?|resolve[sd]?)\s+#(\d+)\b', text_lower):
        refs.append({"type": "fix", "issue": int(m.group(1))})
    # See #N, related to #N
    for m in re.finditer(r'(?:see|related|ref|re|track|follow[- ]up)\s+#(\d+)\b', text_lower):
        refs.append({"type": "related", "issue": int(m.group(1))})
    # Standalone #N references
    for m in re.finditer(r'#(\d{3,4})\b', text_lower):
        num = int(m.group(1))
        if not any(r["issue"] == num for r in refs):
            refs.append({"type": "mention", "issue": num})
    return refs


def fetch_merged_prs(limit=100):
    """Fetch merged PRs with basic metadata."""
    output = run_gh("pr", "list", "--state", "merged",
                    "--limit", str(limit),
                    "--json", "number,title,body,mergedAt,author,baseRefName,headRefName,url",
                    timeout=60)
    if not output:
        return []
    return json.loads(output)


def fetch_pr_files(pr_number):
    """Get list of files changed in a PR (from cached git data or API)."""
    # Try git first for merged PRs
    output = run_gh("pr", "view", str(pr_number),
                    "--json", "files,body,title,mergedAt",
                    timeout=30)
    if not output:
        return []
    try:
        data = json.loads(output)
        files = data.get("files", [])
        return [f.get("path", "") for f in files]
    except (json.JSONDecodeError, KeyError):
        return []


def fetch_closed_issues(limit=50):
    """Fetch closed issues."""
    output = run_gh("issue", "list", "--state", "closed",
                    "--limit", str(limit),
                    "--json", "number,title,body,closedAt,state,labels,url",
                    timeout=30)
    if not output:
        return []
    return json.loads(output)


def guess_comps_from_files(files):
    """Guess comp: labels from changed files."""
    comps = set()
    for f in files:
        for c in comp_for_file(f):
            comps.add(c)
    return sorted(comps)


class TraceabilityAnalyzer:
    def __init__(self, pr_limit=100, issue_limit=50):
        self.pr_limit = pr_limit
        self.issue_limit = issue_limit
        self.prs = []
        self.closed_issues = []
        self.pr_files_cache = {}
        self.linked_issues = {}  # issue_number -> pr_number (resolution link)

    def fetch_all(self):
        """Fetch all data with caching."""
        # Merged PRs
        if MERGED_PRS_CACHE.exists() and (time.time() - MERGED_PRS_CACHE.stat().st_mtime) < 3600:
            with open(MERGED_PRS_CACHE) as f:
                self.prs = json.load(f)
            self.prs = self.prs[:self.pr_limit]
        else:
            self.prs = fetch_merged_prs(self.pr_limit)
            if self.prs:
                with open(MERGED_PRS_CACHE, "w") as f:
                    json.dump(self.prs, f, indent=2)

        # PR files cache
        if PR_FILES_CACHE.exists() and (time.time() - PR_FILES_CACHE.stat().st_mtime) < 3600:
            with open(PR_FILES_CACHE) as f:
                self.pr_files_cache = json.load(f)

        # Closed issues
        if CLOSED_ISSUES_CACHE.exists() and (time.time() - CLOSED_ISSUES_CACHE.stat().st_mtime) < 3600:
            with open(CLOSED_ISSUES_CACHE) as f:
                self.closed_issues = json.load(f)
        else:
            self.closed_issues = fetch_closed_issues(self.issue_limit)
            if self.closed_issues:
                with open(CLOSED_ISSUES_CACHE, "w") as f:
                    json.dump(self.closed_issues, f, indent=2)

    def analyze_prs(self):
        """Analyze PR linkage."""
        results = []
        for i, pr in enumerate(self.prs):
            num = pr["number"]
            body = pr.get("body") or ""
            title = pr.get("title") or ""
            text = title + "\n" + body

            refs = extract_all_issue_refs(text)
            fix_refs = [r for r in refs if r["type"] == "fix"]
            all_refs_types = [r["type"] for r in refs]

            # Track which issues this PR fixes
            for r in fix_refs:
                self.linked_issues[r["issue"]] = {
                    "pr_number": num,
                    "pr_title": title,
                    "type": "fix",
                }

            # Get files for subsystem exposure analysis
            files = self.pr_files_cache.get(str(num))
            if files is None:
                files = fetch_pr_files(num)
                self.pr_files_cache[str(num)] = files
                if len(self.pr_files_cache) % 10 == 0:
                    with open(PR_FILES_CACHE, "w") as f:
                        json.dump(self.pr_files_cache, f, indent=2)

            comps = guess_comps_from_files(files or [])

            merged_at = pr.get("mergedAt", "")
            month = merged_at[:7] if merged_at else "unknown"

            results.append({
                "number": num,
                "title": title,
                "mergedAt": merged_at,
                "month": month,
                "author": pr.get("author", {}).get("login", "unknown"),
                "has_fix_ref": bool(fix_refs),
                "fix_refs": [r["issue"] for r in fix_refs],
                "all_refs": [r["issue"] for r in refs],
                "ref_types": all_refs_types,
                "files_changed": len(files or []),
                "comps": comps,
            })

        # Save updated file cache
        with open(PR_FILES_CACHE, "w") as f:
            json.dump(self.pr_files_cache, f, indent=2)

        return results

    def analyze_closed_issues(self):
        """Check if closed issues have resolution linkage."""
        results = []
        for issue in self.closed_issues:
            num = issue["number"]
            body = issue.get("body") or ""
            title = issue.get("title") or ""
            text = title + "\n" + body

            # Check if issue mentions its own resolution
            refs = extract_all_issue_refs(text)
            pr_refs = [r for r in refs if r["type"] == "fix"]

            # Check if any PR linked this issue
            linked_pr = self.linked_issues.get(num)
            has_pr_link = linked_pr is not None

            results.append({
                "number": num,
                "title": title,
                "closedAt": issue.get("closedAt", ""),
                "self_reports_fix": bool(pr_refs),
                "has_pr_linkage": has_pr_link,
                "linked_pr": linked_pr["pr_number"] if linked_pr else None,
                "labels": [l["name"] for l in issue.get("labels", [])],
            })

        return results

    def compute_metrics(self, pr_results, issue_results):
        """Compute aggregate metrics."""
        total_prs = len(pr_results)
        with_fix_ref = sum(1 for p in pr_results if p["has_fix_ref"])
        with_any_ref = sum(1 for p in pr_results if p["all_refs"])
        orphaned = [p for p in pr_results if not p["all_refs"]]

        # Linkage by month
        monthly = defaultdict(lambda: {"total": 0, "linked": 0})
        for p in pr_results:
            monthly[p["month"]]["total"] += 1
            if p["has_fix_ref"]:
                monthly[p["month"]]["linked"] += 1

        # Orphaned PRs by subsystem
        comp_orphan_count = defaultdict(int)
        comp_total_count = defaultdict(int)
        for p in pr_results:
            for c in p["comps"]:
                comp_total_count[c] += 1
                if not p["all_refs"]:
                    comp_orphan_count[c] += 1

        # Subsystem exposure
        subsystem_exposure = {}
        for c in sorted(comp_total_count.keys()):
            total = comp_total_count[c]
            orphaned_c = comp_orphan_count.get(c, 0)
            subsystem_exposure[c] = {
                "total_prs": total,
                "orphaned_prs": orphaned_c,
                "orphan_pct": round(orphaned_c / total * 100, 1) if total > 0 else 0,
            }

        # Closed issue traceability
        total_closed = len(issue_results)
        with_linkage = sum(1 for i in issue_results if i["has_pr_linkage"] or i["self_reports_fix"])

        # Top orphaned PR authors
        author_orphan_count = Counter(p["author"] for p in orphaned)

        return {
            "pr_metrics": {
                "total_analyzed": total_prs,
                "with_fixes_ref": with_fix_ref,
                "linkage_rate_pct": round(with_fix_ref / total_prs * 100, 1) if total_prs > 0 else 0,
                "with_any_ref": with_any_ref,
                "any_ref_rate_pct": round(with_any_ref / total_prs * 100, 1) if total_prs > 0 else 0,
                "orphaned_count": len(orphaned),
                "orphaned_rate_pct": round(len(orphaned) / total_prs * 100, 1) if total_prs > 0 else 0,
            },
            "monthly_linkage": dict(monthly),
            "subsystem_exposure": subsystem_exposure,
            "top_orphan_authors": author_orphan_count.most_common(10),
            "issue_traceability": {
                "total_closed": total_closed,
                "with_linkage": with_linkage,
                "traceability_rate_pct": round(with_linkage / total_closed * 100, 1) if total_closed > 0 else 0,
                "without_linkage": total_closed - with_linkage,
            },
        }


def generate_report(pr_results, issue_results, metrics):
    """Generate the traceability report."""
    lines = []
    lines.append("=" * 72)
    lines.append("AESTRA PR TRACEABILITY REPORT — Phase 1c")
    lines.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S UTC')}")
    lines.append(f"Scope: last {metrics['pr_metrics']['total_analyzed']} merged PRs")
    lines.append("=" * 72)
    lines.append("")

    pm = metrics["pr_metrics"]

    lines.append("=== SECTION 1: OVERALL LINKAGE ===")
    lines.append("-" * 72)
    lines.append(f"  Merged PRs analyzed:       {pm['total_analyzed']}")
    lines.append(f"  With Fixes #N:             {pm['with_fixes_ref']} ({pm['linkage_rate_pct']}%)")
    lines.append(f"  With any issue reference:  {pm['with_any_ref']} ({pm['any_ref_rate_pct']}%)")
    lines.append(f"  No issue reference:        {pm['orphaned_count']} ({pm['orphaned_rate_pct']}%)")
    lines.append("")

    lines.append("=== SECTION 2: MONTHLY LINKAGE TREND ===")
    lines.append("-" * 72)
    for month in sorted(metrics["monthly_linkage"].keys()):
        m = metrics["monthly_linkage"][month]
        rate = round(m["linked"] / m["total"] * 100, 1) if m["total"] > 0 else 0
        bar = "#" * min(m["total"], 30)
        linked_bar = "#" * max(1, round(m["linked"] / max(m["total"], 1) * 10))
        lines.append(f"  {month}: {m['total']:3d} PRs, {m['linked']} linked ({rate}%) {bar}")
    lines.append("")

    lines.append("=== SECTION 3: SUBSYSTEM EXPOSURE ===")
    lines.append("-" * 72)
    lines.append(f"{'Subsystem':25s} {'Total':>6s} {'Orphaned':>9s} {'Orphan%':>8s}")
    lines.append("-" * 50)
    se = metrics["subsystem_exposure"]
    for comp in sorted(se.keys(), key=lambda c: -se[c]["total_prs"]):
        s = se[comp]
        lines.append(f"  {comp:25s} {s['total_prs']:6d} {s['orphaned_prs']:9d} {s['orphan_pct']:7.1f}%")
    lines.append("")

    lines.append("=== SECTION 4: ISSUE TRACEABILITY ===")
    lines.append("-" * 72)
    it = metrics["issue_traceability"]
    lines.append(f"  Closed issues analyzed:       {it['total_closed']}")
    lines.append(f"  With linked PR:               {it['with_linkage']} ({it['traceability_rate_pct']}%)")
    lines.append(f"  No traceable resolution:      {it['without_linkage']}")
    lines.append("")

    if issue_results:
        untraced = [i for i in issue_results if not i["has_pr_linkage"] and not i["self_reports_fix"]]
        if untraced:
            lines.append("  Closed issues without traceable resolution:")
            for i in untraced[:15]:
                ln = i.get('labels', [])
                ln_str = ", ".join(ln[:3])
                lines.append(f"    #{i['number']} — {i['title'][:60]}")
                if ln_str:
                    lines.append(f"           Labels: {ln_str}")
            if len(untraced) > 15:
                lines.append(f"    ... and {len(untraced) - 15} more")
    lines.append("")

    lines.append("=== SECTION 5: ORPHANED PR DETAILS (top 20 by size) ===")
    lines.append("-" * 72)
    orphaned_with_files = [p for p in pr_results if not p["all_refs"] and p["files_changed"] > 0]
    orphaned_with_files.sort(key=lambda p: -p["files_changed"])
    for p in orphaned_with_files[:20]:
        lines.append(f"  #{p['number']} — {p['title'][:65]}")
        lines.append(f"       Files: {p['files_changed']}, Comps: {', '.join(p['comps'][:3])}")
        lines.append(f"       Author: {p['author']}, Merged: {p['mergedAt'][:10]}")
    if len(orphaned_with_files) > 20:
        lines.append(f"       ... and {len(orphaned_with_files) - 20} more")
    lines.append("")

    lines.append("=" * 72)
    lines.append("END OF REPORT")
    return "\n".join(lines)


def main():
    pr_limit = 100
    for arg in sys.argv[1:]:
        if arg.startswith("--pr-limit="):
            pr_limit = int(arg.split("=")[1])

    force_refresh = "--refresh" in sys.argv
    if force_refresh:
        for f in [MERGED_PRS_CACHE, CLOSED_ISSUES_CACHE, PR_FILES_CACHE]:
            if f.exists():
                f.unlink()

    analyzer = TraceabilityAnalyzer(pr_limit=pr_limit)
    print("Fetching PRs and issues...")
    analyzer.fetch_all()
    print(f"  PRs: {len(analyzer.prs)}, Closed issues: {len(analyzer.closed_issues)}")

    print("\nAnalyzing PR linkage...")
    pr_results = analyzer.analyze_prs()
    linked_count = sum(1 for p in pr_results if p["has_fix_ref"])
    print(f"  Linked: {linked_count}/{len(pr_results)}")

    print("\nAnalyzing issue closure traceability...")
    issue_results = analyzer.analyze_closed_issues()

    print("\nComputing metrics...")
    metrics = analyzer.compute_metrics(pr_results, issue_results)

    report = generate_report(pr_results, issue_results, metrics)
    print("\n" + report)

    report_path = REPO_ROOT / "audit_results.txt"
    with open(report_path, "w") as f:
        f.write(report)

    data_path = CACHE_DIR / "traceability.json"
    with open(data_path, "w") as f:
        json.dump({
            "timestamp": datetime.now().isoformat(),
            "metrics": metrics,
            "pr_results": pr_results,
            "issue_results": issue_results,
        }, f, indent=2)

    print(f"\nReport saved to {report_path}")
    print(f"Data saved to {data_path}")


if __name__ == "__main__":
    main()
