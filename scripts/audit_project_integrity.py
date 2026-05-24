#!/usr/bin/env python3
"""
Phase 1b — Project + Linkage Integrity Check

Verifies that GitHub Projects accurately represents repo reality.

Checks:
  1. Which open issues are missing from the project board (orphaned issues)
  2. Which board items have no linked issue (orphaned board items)
  3. Label↔board field consistency (priority, component, beta-blocker)
  4. Canonical label re-validation (read-only drift check)
  5. Coverage metrics snapshot

Usage:
  python3 scripts/audit_project_integrity.py
  python3 scripts/audit_project_integrity.py --project-number 3
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
PROJECT_CACHE = CACHE_DIR / "project_items.json"
SCHEMA_CACHE = CACHE_DIR / "schema.json"


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
            return None
        return result.stdout
    except subprocess.TimeoutExpired:
        return None


def run_graphql(query, timeout=30):
    """Run a GraphQL query via gh api."""
    try:
        result = subprocess.run(
            ["gh", "api", "graphql", "-f", f"query={query}"],
            capture_output=True, text=True, timeout=timeout,
        )
        if result.returncode != 0:
            print(f"  [WARN] GraphQL error: {result.stderr[:200]}", file=sys.stderr)
            return None
        return json.loads(result.stdout)
    except subprocess.TimeoutExpired:
        return None
    except json.JSONDecodeError:
        return None


def get_project_node_id(project_number):
    """Get the project node ID for a given project number."""
    data = run_graphql(f"""
        {{
          user(login: "currentsuspect") {{
            projectsV2(first: 10) {{
              nodes {{
                number
                id
                title
              }}
            }}
          }}
        }}
    """)
    if not data:
        return None, None
    nodes = data.get("data", {}).get("user", {}).get("projectsV2", {}).get("nodes", [])
    for n in nodes:
        if n and n.get("number") == project_number:
            return n["id"], n["title"]
    return None, None


def fetch_project_items(project_id, force=False):
    """Fetch all items from a project board with field values."""
    if not force and PROJECT_CACHE.exists():
        age = time.time() - PROJECT_CACHE.stat().st_mtime
        if age < 3600:
            with open(PROJECT_CACHE) as f:
                return json.load(f)

    items = []
    cursor = None
    page = 0

    while True:
        after = f'after: "{cursor}"' if cursor else ""
        query = f"""
        {{
          node(id: "{project_id}") {{
            ... on ProjectV2 {{
              items(first: 100 {after}) {{
                totalCount
                pageInfo {{
                  hasNextPage
                  endCursor
                }}
                nodes {{
                  id
                  content {{
                    __typename
                    ... on Issue {{
                      number
                      title
                      state
                      labels(first: 20) {{
                        nodes {{ name }}
                      }}
                    }}
                    ... on PullRequest {{
                      number
                      title
                      state
                    }}
                    ... on DraftIssue {{
                      title
                    }}
                  }}
                  fieldValues(first: 20) {{
                    nodes {{
                      __typename
                      ... on ProjectV2ItemFieldTextValue {{
                        text
                        field {{ ... on ProjectV2FieldCommon {{ name }} }}
                      }}
                      ... on ProjectV2ItemFieldSingleSelectValue {{
                        name
                        field {{ ... on ProjectV2FieldCommon {{ name }} }}
                      }}
                      ... on ProjectV2ItemFieldDateValue {{
                        date
                        field {{ ... on ProjectV2FieldCommon {{ name }} }}
                      }}
                    }}
                  }}
                }}
              }}
            }}
          }}
        }}
        """
        data = run_graphql(query, timeout=60)
        if not data:
            break

        proj_node = data.get("data", {}).get("node", {})
        if not proj_node:
            break

        items_data = proj_node.get("items", {})
        page_items = items_data.get("nodes", [])
        items.extend(page_items)
        page += 1

        page_info = items_data.get("pageInfo", {})
        if not page_info.get("hasNextPage"):
            break
        cursor = page_info.get("endCursor")

        if page >= 10:
            print("  [WARN] Too many pages, stopping at 1000 items", file=sys.stderr)
            break

    with open(PROJECT_CACHE, "w") as f:
        json.dump(items, f, indent=2)

    return items


def fetch_open_issues():
    """Get all open issues from the repo."""
    output = run_gh("issue", "list", "--state", "open",
                    "--limit", "1000",
                    "--json", "number,title,labels,state,assignees,updatedAt",
                    timeout=30)
    if not output:
        return []
    return json.loads(output)


def label_to_board_priority(label):
    """Map GitHub priority label to board Priority field."""
    mapping = {
        "priority: critical": "P0: Critical",
        "priority: high": "P1: High",
        "priority: medium": "P2: Medium",
        "priority: low": "P3: Low",
    }
    return mapping.get(label)


def label_to_board_component(label):
    """Map GitHub comp label to board Component field."""
    mapping = {
        "comp: audio-engine": "Audio Engine",
        "comp: dsp": "DSP",
        "comp: serialization": "Serialization",
        "comp: routing": "Routing",
        "comp: ui": "UI",
        "comp: platform": "Platform",
        "comp: ci": "CI",
        "comp: monetization": "Monetization",
        "comp: export": "Export",
    }
    return mapping.get(label)


def label_to_board_beta(label):
    """Check if beta-blocker label maps to board's Beta Blocker field."""
    return "Yes" if label == "beta-blocker" else None


def check_consistency(issue, board_fields):
    """Check if GitHub labels match project board field values."""
    inconsistencies = []
    labels = [l["name"] for l in issue.get("labels", [])]

    # Priority consistency
    board_priority = board_fields.get("Priority", "")
    label_priorities = [l for l in labels if l.startswith("priority:")]
    if label_priorities:
        expected = label_to_board_priority(label_priorities[0])
        if expected and board_priority and board_priority != expected:
            inconsistencies.append({
                "field": "Priority",
                "label_value": expected,
                "board_value": board_priority,
            })
    elif board_priority and board_priority != "None":
        inconsistencies.append({
            "field": "Priority",
            "label_value": "(missing)",
            "board_value": board_priority,
        })

    # Component consistency
    board_component = board_fields.get("Component", "")
    label_comps = [l for l in labels if l.startswith("comp:")]
    if label_comps:
        expected = [label_to_board_component(l) for l in label_comps]
        expected = [e for e in expected if e]
        if board_component and board_component != "None":
            if expected and board_component not in expected:
                inconsistencies.append({
                    "field": "Component",
                    "label_value": expected,
                    "board_value": board_component,
                })

    # Beta Blocker consistency
    board_beta = board_fields.get("Beta Blocker", "")
    has_beta_label = "beta-blocker" in labels
    label_beta = "Yes" if has_beta_label else ""
    if board_beta and board_beta != "None":
        if label_beta == "Yes" and board_beta != "Yes":
            inconsistencies.append({
                "field": "Beta Blocker",
                "label_value": "Yes",
                "board_value": board_beta,
            })
        elif not has_beta_label and board_beta == "Yes":
            inconsistencies.append({
                "field": "Beta Blocker",
                "label_value": "No",
                "board_value": board_beta,
            })

    return inconsistencies


def check_canonical_labels(labels, schema):
    """Re-validate canonical label compliance (read-only)."""
    issues = []
    dep_map = schema["deprecated"]["migration_map"]
    canonical = schema["categories"]
    canon_set = set()
    for cat in canonical.values():
        canon_set.update(v["name"] for v in cat["values"])
    flag_set = {f["name"] for f in schema["flags"]["values"]}
    all_canon = canon_set | flag_set

    # Check for deprecated labels
    for label in labels:
        if label in dep_map:
            issues.append(f"deprecated:{label}")

    # Check required categories with cardinality
    count_type = sum(1 for l in labels if l.startswith("type:"))
    count_priority = sum(1 for l in labels if l.startswith("priority:"))
    count_comp = sum(1 for l in labels if l.startswith("comp:"))
    count_status = sum(1 for l in labels if l.startswith("status:"))

    if count_type == 0:
        issues.append("missing-type")
    elif count_type > 1:
        issues.append("multiple-type")
    if count_priority == 0:
        issues.append("missing-priority")
    elif count_priority > 1:
        issues.append("multiple-priority")
    if count_comp == 0:
        issues.append("missing-comp")
    if count_status > 1:
        issues.append("multiple-status")

    # Check unknown labels
    for label in labels:
        if label not in all_canon and label not in dep_map:
            issues.append(f"unknown:{label}")

    return issues


def generate_report(results):
    """Generate the integrity report."""
    lines = []
    lines.append("=" * 72)
    lines.append("AESTRA PROJECT BOARD INTEGRITY REPORT — Phase 1b")
    lines.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S UTC')}")
    lines.append("=" * 72)
    lines.append("")

    hard_orphans = results.get("orphaned_issues", [])
    hard_board_orphans = results.get("orphaned_board_items", [])
    inconsistencies = results.get("inconsistencies", [])
    canonical_issues = results.get("canonical_issues", [])
    board_open = results.get("board_open_count", 0)
    repo_open = results.get("repo_open_count", 0)

    lines.append("=== SECTION 1: HARD ERRORS ===")
    lines.append("")

    if hard_orphans:
        lines.append(f"Open issues NOT in project board ({len(hard_orphans)}):")
        lines.append("-" * 72)
        for o in hard_orphans:
            labels_str = ", ".join(o["labels"])
            lines.append(f"  #{o['number']} — {o['title'][:65]}")
            lines.append(f"       Labels: {labels_str}")
            lines.append("")
    else:
        lines.append("  No orphaned issues — all open issues are in the project board.")

    if hard_board_orphans:
        lines.append(f"")
        lines.append(f"Board items with no linked issue ({len(hard_board_orphans)}):")
        lines.append("-" * 72)
        for o in hard_board_orphans:
            lines.append(f"  {o}")
        lines.append("")
    else:
        lines.append("  No orphaned board items.")

    if canonical_issues:
        lines.append("")
        lines.append(f"Canonical label violations in project ({len(canonical_issues)}):")
        lines.append("-" * 72)
        for c in canonical_issues:
            lines.append(f"  #{c['number']} — {c['title'][:65]}")
            for v in c["violations"]:
                lines.append(f"       ⚠  {v}")
            lines.append("")

    lines.append("")
    lines.append("=== SECTION 2: CONSISTENCY WARNINGS ===")
    lines.append("")

    if inconsistencies:
        lines.append(f"Issues where labels differ from board fields ({len(inconsistencies)}):")
        lines.append("-" * 72)
        for inc in inconsistencies:
            lines.append(f"  #{inc['number']} — {inc['title'][:65]}")
            lines.append(f"       Field: {inc['field']}")
            lines.append(f"       Label says:  {inc['label_value']}")
            lines.append(f"       Board says:  {inc['board_value']}")
            lines.append("")
    else:
        lines.append("  No label↔board inconsistencies found.")

    lines.append("")
    lines.append("=== SECTION 3: METRICS SNAPSHOT ===")
    lines.append("-" * 72)
    lines.append("")

    total_open = repo_open
    in_project = board_open
    pct = (in_project / total_open * 100) if total_open > 0 else 0

    lines.append(f"  Open issues (repo):           {total_open}")
    lines.append(f"  Open issues (project board):  {in_project}")
    lines.append(f"  Issue coverage:               {pct:.1f}%")
    lines.append(f"  Orphaned issues:              {len(hard_orphans)}")
    lines.append(f"  Orphaned board items:         {len(hard_board_orphans)}")
    lines.append(f"  Label↔board mismatches:       {len(inconsistencies)}")
    lines.append(f"  Canonical label violations:   {len(canonical_issues)}")
    lines.append("")

    return "\n".join(lines)


def main():
    project_number = 3
    for arg in sys.argv[1:]:
        if arg.startswith("--project-number="):
            project_number = int(arg.split("=")[1])

    schema = load_schema()

    print(f"Fetching project #{project_number}...")
    project_id, project_title = get_project_node_id(project_number)
    if not project_id:
        print(f"ERROR: Could not find project #{project_number}", file=sys.stderr)
        sys.exit(1)
    print(f"  Project: {project_title}")

    items = fetch_project_items(project_id, force="--refresh" in sys.argv)
    print(f"  Items in project: {len(items)}")

    print("\nFetching open issues...")
    open_issues = fetch_open_issues()
    print(f"  Open issues in repo: {len(open_issues)}")

    # Build board state
    issues_in_board = {}
    board_orphans = []

    for item in items:
        content = item.get("content") or {}
        typename = content.get("__typename", "")

        # Extract field values
        fvs = item.get("fieldValues", {}).get("nodes", [])
        board_fields = {}
        for fv in fvs:
            if fv:
                field_name = fv.get("field", {}).get("name", "")
                val = fv.get("name") or fv.get("text") or ""
                board_fields[field_name] = val

        if typename == "Issue":
            num = content.get("number")
            state = content.get("state")
            labels = [l["name"] for l in (content.get("labels", {}).get("nodes", []))]
            issues_in_board[num] = {
                "state": state,
                "labels": labels,
                "board_fields": board_fields,
                "title": content.get("title", ""),
            }
        elif typename in ("PullRequest", "DraftIssue"):
            pass
        else:
            # Item with no content — orphaned board item
            board_orphans.append(f"Board item {item.get('id', '?')} — no linked content")

    # 1. Find orphaned issues (open issues not in project)
    orphaned = []
    for issue in open_issues:
        num = issue["number"]
        if num not in issues_in_board:
            orphaned.append({
                "number": num,
                "title": issue["title"],
                "labels": [l["name"] for l in issue["labels"]],
            })

    # 2. Count open issues actually in project
    board_open = sum(1 for v in issues_in_board.values() if v["state"] == "OPEN")

    # 3. Check label↔board consistency for open issues in project
    inconsistencies = []
    for issue in open_issues:
        num = issue["number"]
        if num in issues_in_board:
            board_data = issues_in_board[num]
            if board_data["state"] != "OPEN":
                continue
            board_fields = board_data["board_fields"]
            incs = check_consistency(issue, board_fields)
            for inc in incs:
                inc["number"] = num
                inc["title"] = issue["title"]
            inconsistencies.extend(incs)

    # 4. Canonical label check for issues in project
    canonical_issues = []
    for num, data in issues_in_board.items():
        if data["state"] != "OPEN":
            continue
        violations = check_canonical_labels(data["labels"], schema)
        if violations:
            canonical_issues.append({
                "number": num,
                "title": data["title"],
                "labels": data["labels"],
                "violations": violations,
            })

    results = {
        "project_number": project_number,
        "project_title": project_title,
        "repo_open_count": len(open_issues),
        "board_open_count": board_open,
        "board_total_items": len(items),
        "orphaned_issues": orphaned,
        "orphaned_board_items": board_orphans,
        "inconsistencies": inconsistencies,
        "canonical_issues": canonical_issues,
    }

    report = generate_report(results)
    print()
    print(report)

    # Save
    report_path = REPO_ROOT / "audit_results.txt"
    with open(report_path, "w") as f:
        f.write(report)
    print(f"Report saved to {report_path}")

    results_path = CACHE_DIR / "project_integrity.json"
    with open(results_path, "w") as f:
        json.dump(results, f, indent=2)
    print(f"Data saved to {results_path}")


if __name__ == "__main__":
    main()
