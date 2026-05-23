#!/usr/bin/env python3
"""
Phase 1a — Issue Tracker Hygiene Audit

Audits Aestra's open issues for:
  - Label consistency against canonical schema
  - Deprecated/legacy labels
  - Missing required labels
  - PR/commit linkage (likely-fixed detection)
  - Staleness
  - Sprint readiness classification

Outputs: JSON data file + human-readable report.

Usage:
  python3 scripts/audit_issues.py
  python3 scripts/audit_issues.py --report-only   # re-render report from cached data
  python3 scripts/audit_issues.py --fetch-only    # fetch data without full report
"""

import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA_PATH = REPO_ROOT / "scripts" / "canonical_schema.json"
CACHE_DIR = REPO_ROOT / ".aestra" / "audit"
CACHE_DIR.mkdir(parents=True, exist_ok=True)
ISSUES_CACHE = CACHE_DIR / "issues.json"
LABELS_CACHE = CACHE_DIR / "labels.json"
REPORT_PATH = REPO_ROOT / "audit_results.txt"


def load_schema():
    with open(SCHEMA_PATH) as f:
        return json.load(f)


def run_gh(*args, timeout=30):
    try:
        result = subprocess.run(
            ["gh", *args],
            capture_output=True, text=True, timeout=timeout,
        )
        if result.returncode != 0:
            print(f"  [WARN] gh {' '.join(args)} failed: {result.stderr.strip()}", file=sys.stderr)
            return None
        return result.stdout
    except subprocess.TimeoutExpired:
        print(f"  [WARN] gh {' '.join(args)} timed out", file=sys.stderr)
        return None


def fetch_all_issues():
    """Fetch all open issues with full metadata via gh."""
    print("Fetching open issues...")
    output = run_gh("issue", "list", "--state", "open",
                    "--limit", "100",
                    "--json", "number,title,labels,state,assignees,updatedAt,milestone,body,comments,url",
                    timeout=60)
    if not output:
        print("ERROR: Could not fetch issues from GitHub", file=sys.stderr)
        return []
    issues = json.loads(output)
    print(f"  Found {len(issues)} open issues")
    return issues


def fetch_issue_comments(issue_number):
    """Fetch comments for a specific issue."""
    output = run_gh("issue", "view", str(issue_number),
                    "--json", "comments",
                    "--jq", ".comments",
                    timeout=30)
    if not output:
        return []
    return json.loads(output)


def has_fix_signal(text, issue_number):
    """Check if text contains an explicit fix reference to the issue number."""
    if not text:
        return False
    patterns = [
        rf'(?:fix(?:es|ed)?|close[sd]?|resolve[sd]?|address[es]?)\s+#{issue_number}\b',
        rf'#{issue_number}\s+(?:fix|close|resolve)',
        rf'(?:fix|close|resolve)\s+issue\s+#{issue_number}\b',
        rf'#{issue_number}\s+is\s+(?:fixed|resolved|closed)',
    ]
    text_lower = text.lower()
    for p in patterns:
        if re.search(p, text_lower):
            return True
    return False


def search_merged_prs(issue_number):
    """Search for merged PRs that explicitly fix/resolve this issue."""
    output = run_gh("pr", "list", "--state", "merged",
                    "--search", f"#{issue_number} in:body",
                    "--json", "number,title,body,mergedAt,url",
                    "--limit", "10",
                    timeout=30)
    if not output:
        return None
    try:
        prs = json.loads(output)
    except json.JSONDecodeError:
        return None

    result = []
    for pr in prs:
        body = pr.get("body") or ""
        title = pr.get("title") or ""
        has_fix = has_fix_signal(body, issue_number) or has_fix_signal(title, issue_number)
        result.append({
            "number": pr["number"],
            "title": pr["title"],
            "url": pr.get("url"),
            "mergedAt": pr.get("mergedAt"),
            "has_fix_signal": has_fix,
        })

    return result if result else None


def search_commits_ref_issue(issue_number):
    """Search recent commits mentioning the issue number."""
    try:
        result = subprocess.run(
            ["git", "log", "--all", "--oneline", "--grep", f"#{issue_number}",
             "--max-count", "20"],
            capture_output=True, text=True, timeout=10,
            cwd=REPO_ROOT,
        )
        if result.returncode == 0 and result.stdout.strip():
            lines = result.stdout.strip().split("\n")
            commits = []
            for line in lines:
                parts = line.split(" ", 1)
                if len(parts) == 2:
                    commits.append({"sha": parts[0], "message": parts[1]})
            return commits
        return None
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None


def check_file_exists(filepath):
    """Check if a source file referenced in an issue still exists.
    Considers common alternative base directories (files may have moved)."""
    direct = REPO_ROOT / filepath
    if direct.exists():
        return True

    alt_map = {
        "AestraAudio/src/Core/": "Source/Core/",
        "AestraAudio/include/Core/": "Source/Core/",
        "AestraAudio/src/Playback/": "Source/Playback/",
        "AestraAudio/src/Plugin/": "Source/Plugin/",
        "AestraAudio/include/Plugin/": "Source/Plugin/",
    }
    for from_prefix, to_prefix in alt_map.items():
        if filepath.startswith(from_prefix):
            alt = REPO_ROOT / filepath.replace(from_prefix, to_prefix, 1)
            if alt.exists():
                return True

    return False


def extract_file_refs(body):
    """Extract file paths from issue body (e.g., AestraAudio/src/...)."""
    if not body:
        return []
    pattern = r'(?:AestraAudio|AestraUI|AestraCore|AestraPlat|Source|Tests|scripts)/[^\s\)]+\.(?:cpp|h|hpp|py|md|yml|yaml|txt|json|cfg)'
    matches = re.findall(pattern, body)
    return list(set(matches))


def extract_todo_refs(body):
    """Extract TODO/FIXME/HACK references from issue body."""
    if not body:
        return []
    patterns = [
        r'(TODO[^:\n]*)',
        r'(FIXME[^:\n]*)',
        r'(HACK[^:\n]*)',
        r'(WORKAROUND[^:\n]*)',
    ]
    refs = []
    for p in patterns:
        matches = re.findall(p, body, re.IGNORECASE)
        refs.extend(m.strip() for m in matches)
    return refs


def extract_pr_refs(text):
    """Extract PR references (#1234) from text."""
    if not text:
        return []
    return [int(m) for m in re.findall(r'#(\d{3,5})', text)]


def classify_verification_mode(issue, schema):
    """Determine verification mode for an issue."""
    labels = [l["name"] for l in issue.get("labels", [])]
    comps = [l for l in labels if l.startswith("comp:")]
    body = issue.get("body") or ""

    # Human-required signals
    human_signals = [
        "CLAP host" in body or "CLAPHost" in body,
        "pan law" in body.lower(),
        "latency" in body.lower() and "? - " not in body,
        "dsp" in body.lower() and "benchmark" not in body.lower(),
        "per-sample" in body.lower(),
        "subjective" in body.lower(),
        any(c in comps for c in ["comp: audio-engine", "comp: dsp"]),
    ]

    if sum(human_signals) >= 2:
        return "human-required"
    if any(c in comps for c in ["comp: serialization", "comp: ci"]):
        return "headless-verifiable"
    if any(c in comps for c in ["comp: ui", "comp: platform"]):
        return "ui-required"
    if any(c in comps for c in ["comp: routing"]):
        return "human-required"
    return "analysis-only"


def analyze_issue(issue, schema, caches):
    """Run all audit checks on a single issue."""
    number = issue["number"]
    title = issue["title"]
    labels = [l["name"] for l in issue.get("labels", [])]
    body = issue.get("body") or ""
    updated = issue.get("updatedAt", "")
    assignees = [a["login"] for a in (issue.get("assignees") or [])]
    comment_list = issue.get("comments") or []
    comment_count = len(comment_list)
    comments_data = caches.get("comments", {}).get(number, [])

    result = {
        "number": number,
        "title": title,
        "url": issue.get("url", f"https://github.com/currentsuspect/Aestra/issues/{number}"),
        "labels": labels,
        "assignees": assignees,
        "updatedAt": updated,
        "comment_count": comment_count,
        "audit": {},
    }

    # --- Label Hygiene ---
    label_issues = []
    canonical = schema["categories"]
    deprecated_map = schema["deprecated"]["migration_map"]
    canonical_type_labels = {v["name"] for cat in canonical.values() for v in cat["values"]}
    flag_labels = {f["name"] for f in schema["flags"]["values"]}
    all_canonical = canonical_type_labels | flag_labels

    has_required_type = False
    has_required_priority = False
    has_required_comp = False
    type_labels_found = []
    priority_labels_found = []
    comp_labels_found = []
    status_labels_found = []
    deprecated_found = []
    flag_labels_found = []
    unknown_labels = []

    for label in labels:
        if label in deprecated_map:
            deprecated_found.append(label)
        elif label in canonical["type"]["values_name_set"]:
            type_labels_found.append(label)
            has_required_type = True
        elif label in canonical["priority"]["values_name_set"]:
            priority_labels_found.append(label)
            has_required_priority = True
        elif label in canonical["comp"]["values_name_set"]:
            comp_labels_found.append(label)
            has_required_comp = True
        elif label in canonical["status"]["values_name_set"]:
            status_labels_found.append(label)
        elif label in flag_labels:
            flag_labels_found.append(label)
        elif label in all_canonical:
            pass
        else:
            unknown_labels.append(label)

    # Check required categories
    if not has_required_type:
        label_issues.append("missing-type")
    if not has_required_priority:
        label_issues.append("missing-priority")
    if not has_required_comp:
        label_issues.append("missing-comp")

    # Check cardinality violations
    if len(type_labels_found) > 1:
        label_issues.append(f"multiple-type-labels:{','.join(type_labels_found)}")
    if len(priority_labels_found) > 1:
        label_issues.append(f"multiple-priority-labels:{','.join(priority_labels_found)}")
    if len(status_labels_found) > 1:
        label_issues.append(f"multiple-status-labels:{','.join(status_labels_found)}")

    # Check deprecated labels
    if deprecated_found:
        for dep in deprecated_found:
            mapping = deprecated_map.get(dep, {})
            target = mapping.get("target")
            if target:
                label_issues.append(f"deprecated-label:{dep}->{target}")
            else:
                label_issues.append(f"deprecated-label:{dep}->needs-review")

    # Check redundant legacy+canonical pairs
    for dep, mapping in deprecated_map.items():
        target = mapping.get("target")
        if target and dep in labels and target in labels:
            if "redundant-pair" not in label_issues:
                label_issues.append(f"redundant-pair:{dep}+{target}")

    result["audit"]["label_issues"] = label_issues
    result["audit"]["suggested_labels"] = _suggest_labels(
        issue, labels, canonical, deprecated_found, deprecated_map
    )

    # --- File References ---
    file_refs = extract_file_refs(body)
    if file_refs:
        dead_files = [f for f in file_refs if not check_file_exists(f)]
        result["audit"]["file_refs"] = {
            "all": file_refs,
            "dead": dead_files,
        }

    # --- TODO References ---
    todo_refs = extract_todo_refs(body)
    if todo_refs:
        result["audit"]["todo_refs"] = todo_refs

    # --- PR/Commit Linkage ---
    pr_refs_from_body = extract_pr_refs(body)
    body_lower = body.lower()
    fixed_keywords = ["fixed", "resolved", "closed by", "fixes"]
    mentions_fix = any(kw in body_lower for kw in fixed_keywords)

    merged_prs = caches.get("prs", {}).get(number, [])
    linked_commits = caches.get("commits", {}).get(number, [])

    linkage = {}
    if merged_prs:
        linkage["merged_prs"] = [
            {"number": p["number"], "title": p["title"], "url": p.get("url")}
            for p in merged_prs
        ]
    if linked_commits:
        linkage["commits"] = linked_commits
    if pr_refs_from_body:
        linkage["body_pr_refs"] = pr_refs_from_body
    if mentions_fix:
        linkage["body_mentions_fix"] = True

    if linkage:
        result["audit"]["linkage"] = linkage

    # --- Likely-Fixed Detection ---
    likely_fixed_signals = []

    pr_fix_signals = []
    if merged_prs:
        for pr in merged_prs:
            if pr.get("has_fix_signal"):
                pr_fix_signals.append(pr["number"])
        if pr_fix_signals:
            likely_fixed_signals.append(f"merged-pr-with-fix-signal:{','.join(str(n) for n in pr_fix_signals)}")

    if merged_prs and not pr_fix_signals:
        if len(merged_prs) == 1 and merged_prs[0]["title"].startswith(("fix", "Fix", "FIX")):
            likely_fixed_signals.append("weak-pr-title-match")

    if mentions_fix and pr_fix_signals:
        likely_fixed_signals.append("issue-body-mentions-fix+linked-pr")

    if linked_commits:
        fix_commits = [c for c in linked_commits if re.search(r'fix(?:es|ed)?|close[sd]?|resolve[sd]?', c["message"], re.IGNORECASE)]
        if fix_commits:
            likely_fixed_signals.append(f"commit-with-fix-keyword:{','.join(c['sha'][:7] for c in fix_commits[:3])}")

    if likely_fixed_signals:
        result["audit"]["likely_fixed"] = {
            "signals": likely_fixed_signals,
            "confidence": _fixed_confidence(likely_fixed_signals),
            "pr_fix_signals": pr_fix_signals,
        }

    # --- Staleness ---
    if updated:
        try:
            updated_dt = datetime.fromisoformat(updated.replace("Z", "+00:00"))
            now = datetime.now(timezone.utc)
            days_since_update = (now - updated_dt).days
            result["audit"]["days_since_update"] = days_since_update

            staleness = []
            if days_since_update > 180:
                staleness.append("very-stale")
            elif days_since_update > 90:
                staleness.append("stale")
            elif days_since_update > 30:
                staleness.append("low-activity")

            if "status: deferred" in labels and days_since_update > 90:
                staleness.append("deferred-unchecked")

            if days_since_update > 60 and not assignees:
                staleness.append("unassigned-stale")

            result["audit"]["staleness"] = staleness if staleness else "active"
        except (ValueError, TypeError):
            result["audit"]["staleness"] = "unknown-date"

    # --- Verification Mode ---
    result["audit"]["verification_mode"] = classify_verification_mode(issue, schema)

    # --- Sprint Readiness ---
    result["audit"]["sprint_ready"] = _sprint_readiness(result, labels)

    return result


def _suggest_labels(issue, labels, canonical, deprecated_found, deprecated_map):
    """Suggest label corrections based on pattern."""
    suggestions = []

    if "missing-type" in str(labels) or not [l for l in labels if l.startswith("type:")]:
        body = issue.get("body", "") or ""
        body_lower = body.lower()
        title_lower = issue.get("title", "").lower()

        if any(w in title_lower for w in ["bug", "crash", "broken", "race", "leak", "corrupt", "wrong"]):
            suggestions.append("type: bug")
        elif any(w in title_lower for w in ["feature", "implement", "support", "missing"]):
            suggestions.append("type: feature")
        elif any(w in title_lower for w in ["refactor", "layering", "move", "clean", "restructure"]):
            suggestions.append("type: refactor")
        elif any(w in title_lower for w in ["perf", "performance", "slow", "fast", "optimize", "batched"]):
            suggestions.append("type: perf")
        elif any(w in title_lower for w in ["ci", "pipeline", "build", "test"]):
            suggestions.append("type: ci")
        elif any(w in title_lower for w in ["hardening", "safety", "rt", "realtime", "protect"]):
            suggestions.append("type: hardening")

    if "missing-priority" in str(labels):
        if any(l.startswith("comp:") and "beta-blocker" in labels for l in labels):
            suggestions.append("priority: high")
        elif "bug" in deprecated_found:
            suggestions.append("priority: medium")

    if "missing-comp" in str(labels):
        body = issue.get("body", "") or ""
        for dep in deprecated_found:
            target = deprecated_map.get(dep, {}).get("target")
            if target and target.startswith("comp:"):
                suggestions.append(target)

    return suggestions


def _fixed_confidence(signals):
    """Compute confidence that an issue is already fixed."""
    confidence = 0.0
    for s in signals:
        if s.startswith("issue-body-mentions-fix+linked-pr") and any(
            "merged-pr-with-fix-signal" in sig for sig in signals
        ):
            confidence = max(confidence, 0.90)
        elif s.startswith("merged-pr-with-fix-signal"):
            confidence = max(confidence, 0.85)
        elif s.startswith("commit-with-fix-keyword"):
            confidence = max(confidence, 0.60)
        elif s == "weak-pr-title-match":
            confidence = max(confidence, 0.30)
        elif s.startswith("has-commits"):
            confidence = max(confidence, 0.40)
    return round(confidence, 2)


def _sprint_readiness(result, labels):
    """Classify issue for sprint readiness."""
    audit = result["audit"]

    if audit.get("likely_fixed", {}).get("confidence", 0) > 0.7:
        return "verify-then-close"
    if "status: deferred" in labels:
        return "not-sprint-material"
    if audit.get("days_since_update", 0) > 180:
        return "stale-review-needed"
    if audit.get("verification_mode") == "human-required":
        return "requires-domain-expert"
    if not audit.get("label_issues"):
        return "sprint-ready"
    return "needs-triage"


def fetch_cached_or_fresh(cache_path, fetch_fn, max_age_hours=1):
    """Fetch data with simple file caching."""
    if cache_path.exists():
        age = time.time() - cache_path.stat().st_mtime
        if age < max_age_hours * 3600:
            with open(cache_path) as f:
                return json.load(f)
    data = fetch_fn()
    if data is not None:
        with open(cache_path, "w") as f:
            json.dump(data, f, indent=2)
    return data


def build_caches(issues, schema):
    """Pre-fetch PR/commit linkage data for all issues."""
    print("\nBuilding PR/commit linkage caches...")
    caches = {"prs": {}, "commits": {}, "comments": {}}
    pr_cache = CACHE_DIR / "prs.json"
    commit_cache = CACHE_DIR / "commits.json"
    comment_cache = CACHE_DIR / "comments.json"

    # Try loading full caches
    if pr_cache.exists() and (time.time() - pr_cache.stat().st_mtime) < 3600:
        with open(pr_cache) as f:
            caches["prs"] = json.load(f)
    if commit_cache.exists() and (time.time() - commit_cache.stat().st_mtime) < 3600:
        with open(commit_cache) as f:
            caches["commits"] = json.load(f)
    if comment_cache.exists() and (time.time() - comment_cache.stat().st_mtime) < 3600:
        with open(comment_cache) as f:
            caches["comments"] = json.load(f)

    needs_fetch = []
    for issue in issues:
        n = issue["number"]
        if n not in caches["prs"] or n not in caches["commits"]:
            needs_fetch.append(n)

    if needs_fetch:
        print(f"  Fetching linkage for {len(needs_fetch)} issues...")
        for i, n in enumerate(needs_fetch):
            sys.stdout.write(f"\r    [{i+1}/{len(needs_fetch)}] issue #{n}  ")
            sys.stdout.flush()

            # Search merged PRs
            merged = search_merged_prs(n)
            if merged:
                caches["prs"][n] = merged

            # Search commits
            commits = search_commits_ref_issue(n)
            if commits:
                caches["commits"][n] = commits

            # Get comments
            comments = fetch_issue_comments(n)
            if comments:
                caches["comments"][n] = comments

            if i % 10 == 9:
                time.sleep(0.5)

        print()

    # Cache results
    with open(pr_cache, "w") as f:
        json.dump(caches["prs"], f)
    with open(commit_cache, "w") as f:
        json.dump(caches["commits"], f)
    with open(comment_cache, "w") as f:
        json.dump(caches["comments"], f)

    return caches


def prepare_schema_sets(schema):
    """Add set lookups to schema categories for fast membership testing."""
    for cat in schema["categories"].values():
        cat["values_name_set"] = {v["name"] for v in cat["values"]}
    return schema


def generate_report(results, schema):
    """Generate the human-readable audit report."""
    lines = []
    lines.append("=" * 72)
    lines.append("AESTRA ISSUE TRACKER AUDIT REPORT — Phase 1a")
    lines.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S UTC')}")
    lines.append(f"Schema version: {schema['_meta']['version']}")
    lines.append("=" * 72)
    lines.append("")

    # Summary stats
    total = len(results)
    labeled_ok = sum(1 for r in results if not r["audit"].get("label_issues"))
    labeled_issues = total - labeled_ok
    likely_fixed = sum(1 for r in results if r["audit"].get("likely_fixed", {}).get("confidence", 0) > 0.7)
    stale = sum(1 for r in results if "stale" in str(r["audit"].get("staleness", [])))
    human_review = sum(1 for r in results if r["audit"].get("verification_mode") == "human-required")
    sprint_ready = sum(1 for r in results if r["audit"].get("sprint_ready") == "sprint-ready")
    dead_refs = sum(1 for r in results if r["audit"].get("file_refs", {}).get("dead"))

    lines.append("SUMMARY")
    lines.append("-" * 72)
    lines.append(f"  Total open issues:        {total}")
    lines.append(f"  Label issues:             {labeled_issues}")
    lines.append(f"  Likely-fixed (needs close): {likely_fixed}")
    lines.append(f"  Stale issues:             {stale}")
    lines.append(f"  Needs domain expert:      {human_review}")
    lines.append(f"  Sprint-ready issues:      {sprint_ready}")
    lines.append(f"  Dead file references:     {dead_refs}")
    lines.append("")

    # Section A: Label violations
    lines.append("A. LABEL HYGIENE VIOLATIONS")
    lines.append("-" * 72)
    violations_found = False
    for r in results:
        label_issues = r["audit"].get("label_issues", [])
        if label_issues:
            violations_found = True
            labels_str = ", ".join(r["labels"])
            lines.append(f"  #{r['number']} — {r['title'][:70]}")
            lines.append(f"       Labels: {labels_str}")
            for li in label_issues:
                lines.append(f"       ⚠  {li}")
            if r["audit"].get("suggested_labels"):
                lines.append(f"       → Suggest: {', '.join(r['audit']['suggested_labels'])}")
            lines.append("")
    if not violations_found:
        lines.append("  (none)")
    lines.append("")

    # Section B: Likely-fixed issues
    lines.append("B. LIKELY-FIXED ISSUES (needs verification)")
    lines.append("-" * 72)
    fixed_found = False
    for r in results:
        lf = r["audit"].get("likely_fixed")
        if lf and lf["confidence"] > 0.7:
            fixed_found = True
            lines.append(f"  #{r['number']} — {r['title'][:70]}")
            lines.append(f"       Confidence: {lf['confidence']}")
            for s in lf["signals"]:
                lines.append(f"       Signal: {s}")
            linkage = r["audit"].get("linkage", {})
            if linkage.get("merged_prs"):
                for pr in linkage["merged_prs"]:
                    lines.append(f"       Merged PR: #{pr['number']} — {pr['title'][:60]}")
            lines.append("")
    if not fixed_found:
        lines.append("  (none)")
    lines.append("")

    # Section C: Staleness
    lines.append("C. STALE ISSUES")
    lines.append("-" * 72)
    stale_found = False
    for r in results:
        staleness = r["audit"].get("staleness", [])
        if isinstance(staleness, list) and "very-stale" in staleness:
            stale_found = True
            days = r["audit"].get("days_since_update", "?")
            lines.append(f"  #{r['number']} — {r['title'][:70]}")
            lines.append(f"       Last updated: {days} days ago")
            lines.append(f"       Assignee(s): {r['assignees'] or 'none'}")
            lines.append("")
    if not stale_found:
        lines.append("  (none)")
    lines.append("")

    # Section D: Sprint-ready work
    lines.append("D. SPRINT-READY ISSUES")
    lines.append("-" * 72)
    ready = [r for r in results if r["audit"].get("sprint_ready") == "sprint-ready"]
    if ready:
        for r in ready:
            lines.append(f"  #{r['number']} — {r['title'][:70]}")
            labels_str = ", ".join(r["labels"])
            lines.append(f"       {labels_str}")
            lines.append("")
    else:
        lines.append("  (none)")
    lines.append("")

    # Section E: Dead file references
    lines.append("E. ISSUES WITH DEAD FILE REFERENCES")
    lines.append("-" * 72)
    dead_found = False
    for r in results:
        dead = r["audit"].get("file_refs", {}).get("dead", [])
        if dead:
            dead_found = True
            lines.append(f"  #{r['number']} — {r['title'][:70]}")
            for d in dead:
                lines.append(f"       ✗ {d}")
            lines.append("")
    if not dead_found:
        lines.append("  (none)")
    lines.append("")

    # Section F: Verification modes
    lines.append("F. VERIFICATION MODE DISTRIBUTION")
    lines.append("-" * 72)
    modes = {}
    for r in results:
        mode = r["audit"].get("verification_mode", "unknown")
        modes[mode] = modes.get(mode, 0) + 1
    for mode, count in sorted(modes.items(), key=lambda x: -x[1]):
        lines.append(f"  {mode}: {count}")
    lines.append("")

    # Section G: Sprint readiness distribution
    lines.append("G. SPRINT READINESS DISTRIBUTION")
    lines.append("-" * 72)
    readiness = {}
    for r in results:
        status = r["audit"].get("sprint_ready", "unknown")
        readiness[status] = readiness.get(status, 0) + 1
    for status, count in sorted(readiness.items(), key=lambda x: -x[1]):
        lines.append(f"  {status}: {count}")
    lines.append("")

    # Section H: Hotspot analysis
    lines.append("H. SUBSYSTEM HOTSPOTS")
    lines.append("-" * 72)
    comps = {}
    for r in results:
        for l in r["labels"]:
            if l.startswith("comp:"):
                comps[l] = comps.get(l, 0) + 1
    for comp, count in sorted(comps.items(), key=lambda x: -x[1]):
        bar = "#" * min(count, 30)
        lines.append(f"  {comp:25s} {count:3d} {bar}")
    lines.append("")

    lines.append("=" * 72)
    lines.append("END OF REPORT")
    lines.append("=" * 72)

    return "\n".join(lines)


def main():
    schema = load_schema()
    schema = prepare_schema_sets(schema)

    fetch_only = "--fetch-only" in sys.argv
    report_only = "--report-only" in sys.argv

    if not report_only:
        issues = fetch_all_issues()
        if not issues:
            print("ERROR: No issues fetched, aborting.", file=sys.stderr)
            sys.exit(1)

        with open(ISSUES_CACHE, "w") as f:
            json.dump(issues, f, indent=2)

        labels_data = run_gh("label", "list", "--limit", "100",
                            "--json", "name,description,color",
                            timeout=30)
        if labels_data:
            with open(LABELS_CACHE, "w") as f:
                f.write(labels_data)

        if fetch_only:
            print("Fetch complete. Data cached in .aestra/audit/")
            return

        # Build PR/commit/comments caches
        caches = build_caches(issues, schema)
    else:
        # Load from cache
        if not ISSUES_CACHE.exists():
            print("ERROR: No cached issue data. Run without --report-only first.", file=sys.stderr)
            sys.exit(1)
        with open(ISSUES_CACHE) as f:
            issues = json.load(f)
        caches = build_caches(issues, schema)

    # Analyze
    print(f"\nAnalyzing {len(issues)} issues...")
    results = []
    for issue in issues:
        result = analyze_issue(issue, schema, caches)
        results.append(result)

    # Sort by number
    results.sort(key=lambda r: r["number"])

    # Save detailed results
    results_path = CACHE_DIR / "audit_results.json"
    with open(results_path, "w") as f:
        json.dump(results, f, indent=2)
    print(f"Detailed audit results saved to {results_path}")

    # Generate and save report
    report = generate_report(results, schema)
    with open(REPORT_PATH, "w") as f:
        f.write(report)
    print(f"\nReport saved to {REPORT_PATH}")
    print()
    print(report)


if __name__ == "__main__":
    main()
