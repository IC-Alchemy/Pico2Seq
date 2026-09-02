#!/usr/bin/env python3
"""
verify_docs_links.py - Empirical verification tool for Pico2Seq documentation links,
file paths, anchor targets, code fence balances, and cross-references.
"""

import os
import sys
import re
import urllib.parse
from pathlib import Path

# Ensure UTF-8 stdout on Windows
if sys.platform == 'win32':
    try:
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')
    except Exception:
        pass

ROOT_DIR = Path(__file__).resolve().parent.parent

EXCLUDE_DIRS = {'.git', 'build', 'build_test', '.agents', '.zcode', 'build_host'}

# Markdown link regexes
MD_INLINE_LINK = re.compile(r'!?\[([^\]]*)\]\(([^)]+)\)')
MD_REF_LINK = re.compile(r'\[([^\]]*)\]\[([^\]]*)\]')
MD_REF_DEF = re.compile(r'^\s*\[([^\]]+)\]:\s*(\S+)(?:\s+["\'(](.*)[")\'])?', re.MULTILINE)
HTML_A_HREF = re.compile(r'<a\s+(?:[^>]*?\s+)?href=["\']([^"\']+)["\']', re.IGNORECASE)
HTML_A_NAME = re.compile(r'<a\s+(?:[^>]*?\s+)?(?:id|name)=["\']([^"\']+)["\']', re.IGNORECASE)

# Backticked file path pattern
PATH_PATTERN = re.compile(r'`([a-zA-Z0-9_./\\-]+\.[a-zA-Z0-9_]+)`|`([a-zA-Z0-9_.-]+/[a-zA-Z0-9_./\\-]+)`')

def slugify(text):
    """Generate GitHub-compatible markdown heading slug."""
    text = text.strip()
    # Remove markdown links: [text](url) -> text
    text = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', text)
    # Remove markdown formatting characters: ` * _ ~
    text = re.sub(r'[`*_~]', '', text)
    # Strip HTML tags
    text = re.sub(r'<[^>]+>', '', text)
    # Lowercase
    text = text.lower()
    # Remove punctuation except hyphens, spaces, and alphanumeric
    text = re.sub(r'[^\w\s-]', '', text)
    # Replace whitespace with hyphens
    text = re.sub(r'\s+', '-', text)
    return text

def parse_markdown_headings_and_anchors(file_path):
    """Extract all valid anchors (heading slugs and HTML anchors) from a markdown file."""
    slugs = set()
    slug_counts = {}
    
    if not os.path.isfile(file_path):
        return slugs

    try:
        with open(file_path, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Error reading {file_path}: {e}", file=sys.stderr)
        return slugs

    in_code_block = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('```') or stripped.startswith('~~~'):
            in_code_block = not in_code_block
            continue
        if in_code_block:
            continue

        # Check for HTML anchor IDs/names
        for m in HTML_A_NAME.finditer(stripped):
            slugs.add(m.group(1).lower())
            slugs.add(m.group(1))

        # Check for ATX headings: # Header
        m = re.match(r'^(#{1,6})\s+(.+)$', stripped)
        if m:
            heading_text = m.group(2).strip()
            # Remove trailing hashes: ## Heading ##
            heading_text = re.sub(r'\s*#+$', '', heading_text)
            
            # Also check if heading has custom anchor ID: {#custom-id}
            custom_m = re.search(r'\{#([a-zA-Z0-9_-]+)\}\s*$', heading_text)
            if custom_m:
                slugs.add(custom_m.group(1).lower())
                heading_text = heading_text[:custom_m.start()].strip()

            base_slug = slugify(heading_text)
            if base_slug:
                if base_slug in slug_counts:
                    slug_counts[base_slug] += 1
                    dedup_slug = f"{base_slug}-{slug_counts[base_slug]}"
                else:
                    slug_counts[base_slug] = 0
                    dedup_slug = base_slug
                slugs.add(dedup_slug)
                slugs.add(base_slug)

    return slugs

def find_all_markdown_files():
    """Find all markdown files under ROOT_DIR excluding excluded directories."""
    md_files = []
    for root, dirs, files in os.walk(ROOT_DIR):
        dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS and not any(part in EXCLUDE_DIRS for part in Path(os.path.join(root, d)).parts)]
        for f in files:
            if f.endswith('.md'):
                full_path = os.path.normpath(os.path.join(root, f))
                md_files.append(full_path)
    md_files.sort()
    return md_files

def check_case_sensitive_exists(file_path):
    """
    Check if file exists and verify exact case sensitivity on disk.
    Returns (exists: bool, case_matches: bool, actual_path: str or None)
    """
    if not os.path.exists(file_path):
        return False, False, None
    
    parts = Path(file_path).resolve().parts
    curr = Path(parts[0])
    case_matched = True
    for part in parts[1:]:
        try:
            entries = os.listdir(curr)
            if part not in entries:
                matched = [e for e in entries if e.lower() == part.lower()]
                if matched:
                    case_matched = False
                    curr = curr / matched[0]
                else:
                    return False, False, None
            else:
                curr = curr / part
        except (PermissionError, FileNotFoundError):
            return True, True, str(file_path)
            
    return True, case_matched, str(curr)

def check_code_fence_balance(file_path):
    """Check if code fences (``` and ~~~) are balanced in a markdown file."""
    fences = []
    try:
        with open(file_path, 'r', encoding='utf-8', errors='replace') as f:
            for i, line in enumerate(f, 1):
                stripped = line.strip()
                if stripped.startswith('```') or stripped.startswith('~~~'):
                    fences.append((i, stripped[:3]))
    except Exception as e:
        return False, [f"Could not open file: {e}"]
    
    if len(fences) % 2 != 0:
        return False, [f"Unbalanced code fences: found {len(fences)} fences on lines {[f[0] for f in fences]}"]
    return True, []

def check_stale_legacy_references(file_path):
    """Check for obsolete references like DaisySP, PROGRAMMERS_MANUAL.md, etc."""
    stale_findings = []
    try:
        with open(file_path, 'r', encoding='utf-8', errors='replace') as f:
            for i, line in enumerate(f, 1):
                if 'PROGRAMMERS_MANUAL' in line:
                    stale_findings.append((i, 'PROGRAMMERS_MANUAL reference found', line.strip()))
                if 'daisysp.h' in line or 'src/dsp/' in line:
                    if 'alchemyui-tmag5273-migration.md' not in file_path and 'AGENTS.md' not in file_path:
                        stale_findings.append((i, 'Legacy DaisySP / src/dsp/ path found', line.strip()))
    except Exception:
        pass
    return stale_findings

def main():
    md_files = find_all_markdown_files()
    print("============================================================")
    print(" Pico2Seq Documentation Link & Path Verification Harness")
    print(f" Found {len(md_files)} markdown files across repository.")
    print("============================================================\n")

    heading_cache = {}
    for md in md_files:
        heading_cache[md] = parse_markdown_headings_and_anchors(md)

    total_links = 0
    valid_links = 0
    broken_links = []
    broken_anchors = []
    case_mismatches = []
    external_links = []
    fence_errors = []
    stale_refs = []

    results_by_file = {}

    for md_path in md_files:
        rel_src = os.path.relpath(md_path, ROOT_DIR).replace('\\', '/')
        
        # Check code fence balance
        is_balanced, f_errs = check_code_fence_balance(md_path)
        if not is_balanced:
            fence_errors.append((rel_src, f_errs))

        # Check stale references
        s_refs = check_stale_legacy_references(md_path)
        if s_refs:
            stale_refs.extend([(rel_src, r[0], r[1], r[2]) for r in s_refs])

        results_by_file[rel_src] = {
            'links': [],
            'broken_links': [],
            'broken_anchors': [],
            'case_mismatches': [],
            'external': [],
            'backtick_paths': [],
            'broken_backtick_paths': []
        }

        try:
            with open(md_path, 'r', encoding='utf-8', errors='replace') as f:
                content = f.read()
                lines = content.splitlines()
        except Exception as e:
            print(f"Could not read {md_path}: {e}")
            continue

        ref_defs = {}
        for m in MD_REF_DEF.finditer(content):
            ref_id = m.group(1).strip().lower()
            ref_target = m.group(2).strip()
            ref_defs[ref_id] = ref_target

        in_code_block = False
        for line_num, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith('```') or stripped.startswith('~~~'):
                in_code_block = not in_code_block
                continue
            if in_code_block:
                continue

            # 1. Inline links: [text](target)
            for m in MD_INLINE_LINK.finditer(line):
                link_text = m.group(1).strip()
                link_target = m.group(2).strip()
                target_parts = link_target.split(None, 1)
                clean_target = target_parts[0] if target_parts else link_target
                clean_target = clean_target.strip('<>')

                total_links += 1
                link_info = {
                    'line': line_num,
                    'text': link_text,
                    'target': clean_target,
                    'raw': m.group(0)
                }

                if clean_target.startswith(('http://', 'https://', 'mailto:', 'ftp://')):
                    external_links.append((rel_src, line_num, link_text, clean_target))
                    results_by_file[rel_src]['external'].append(link_info)
                    continue

                parsed = urllib.parse.urlparse(clean_target)
                target_path = parsed.path
                target_anchor = parsed.fragment.lower() if parsed.fragment else None

                if not target_path and target_anchor:
                    available_anchors = heading_cache.get(md_path, set())
                    if target_anchor in available_anchors:
                        valid_links += 1
                        results_by_file[rel_src]['links'].append((link_info, "OK"))
                    else:
                        broken_anchors.append((rel_src, line_num, link_text, clean_target, f"Anchor #{target_anchor} not found in {rel_src}"))
                        results_by_file[rel_src]['broken_anchors'].append((link_info, f"Missing anchor: #{target_anchor}"))
                    continue

                if target_path:
                    target_path = urllib.parse.unquote(target_path)
                    
                    if target_path.startswith('/'):
                        resolved_file = os.path.normpath(os.path.join(ROOT_DIR, target_path.lstrip('/')))
                    else:
                        resolved_file = os.path.normpath(os.path.join(os.path.dirname(md_path), target_path))

                    exists, case_matches, actual = check_case_sensitive_exists(resolved_file)
                    if not exists:
                        broken_links.append((rel_src, line_num, link_text, clean_target, f"Target file does not exist: {resolved_file}"))
                        results_by_file[rel_src]['broken_links'].append((link_info, f"File not found: {clean_target}"))
                    else:
                        if not case_matches:
                            case_mismatches.append((rel_src, line_num, clean_target, actual))
                            results_by_file[rel_src]['case_mismatches'].append((link_info, f"Case mismatch: requested '{clean_target}', actual '{os.path.relpath(actual, ROOT_DIR)}'"))

                        if target_anchor:
                            if resolved_file in heading_cache:
                                target_slugs = heading_cache[resolved_file]
                            else:
                                target_slugs = parse_markdown_headings_and_anchors(resolved_file)
                                heading_cache[resolved_file] = target_slugs

                            if target_anchor in target_slugs:
                                valid_links += 1
                                results_by_file[rel_src]['links'].append((link_info, "OK"))
                            else:
                                broken_anchors.append((rel_src, line_num, link_text, clean_target, f"Anchor #{target_anchor} not found in target file {os.path.relpath(resolved_file, ROOT_DIR)}"))
                                results_by_file[rel_src]['broken_anchors'].append((link_info, f"Missing anchor #{target_anchor} in {os.path.relpath(resolved_file, ROOT_DIR)}"))
                        else:
                            valid_links += 1
                            results_by_file[rel_src]['links'].append((link_info, "OK"))

            # 2. HTML <a> tags with href
            for m in HTML_A_HREF.finditer(line):
                href = m.group(1).strip()
                if href.startswith(('http://', 'https://', 'mailto:', 'ftp://')):
                    external_links.append((rel_src, line_num, "<a> tag", href))
                    continue
                total_links += 1
                link_info = {'line': line_num, 'text': '<a> tag', 'target': href, 'raw': m.group(0)}
                parsed = urllib.parse.urlparse(href)
                target_path = parsed.path
                target_anchor = parsed.fragment.lower() if parsed.fragment else None

                if not target_path and target_anchor:
                    if target_anchor in heading_cache.get(md_path, set()):
                        valid_links += 1
                        results_by_file[rel_src]['links'].append((link_info, "OK"))
                    else:
                        broken_anchors.append((rel_src, line_num, "<a>", href, f"Anchor #{target_anchor} not found"))
                        results_by_file[rel_src]['broken_anchors'].append((link_info, f"Missing anchor: #{target_anchor}"))
                elif target_path:
                    target_path = urllib.parse.unquote(target_path)
                    resolved_file = os.path.normpath(os.path.join(ROOT_DIR if target_path.startswith('/') else os.path.dirname(md_path), target_path.lstrip('/') if target_path.startswith('/') else target_path))
                    exists, case_matches, actual = check_case_sensitive_exists(resolved_file)
                    if not exists:
                        broken_links.append((rel_src, line_num, "<a>", href, f"File not found: {resolved_file}"))
                        results_by_file[rel_src]['broken_links'].append((link_info, f"File not found: {href}"))
                    else:
                        valid_links += 1
                        results_by_file[rel_src]['links'].append((link_info, "OK"))

            # 3. Backtick file path check
            for m in PATH_PATTERN.finditer(line):
                candidate = m.group(1) or m.group(2)
                candidate = candidate.strip()
                if any(candidate.startswith(p) for p in ['src/', 'docs/', 'tests/', 'vendor/']) or candidate.endswith(('.cpp', '.h', '.ino', '.md', '.cmake', '.txt', '.yml', '.json', '.ld')):
                    if not candidate.startswith(('-', 'http', '$', '%')):
                        root_cand = os.path.normpath(os.path.join(ROOT_DIR, candidate))
                        file_cand = os.path.normpath(os.path.join(os.path.dirname(md_path), candidate))
                        exists = os.path.exists(root_cand) or os.path.exists(file_cand)
                        results_by_file[rel_src]['backtick_paths'].append((line_num, candidate, exists))
                        if not exists:
                            if not ('*' in candidate or '{' in candidate or '<' in candidate or '...' in candidate):
                                results_by_file[rel_src]['broken_backtick_paths'].append((line_num, candidate))

    # Print detailed report per file
    print("============================================================")
    print(" DETAILED LINK AUDIT PER FILE")
    print("============================================================")
    for src, data in sorted(results_by_file.items()):
        file_all_links = (
            [(l['line'], 'VALID', l['text'], l['target'], '') for l, s in data['links']] +
            [(l['line'], 'BROKEN_LINK', l['text'], l['target'], reason) for l, reason in data['broken_links']] +
            [(l['line'], 'BROKEN_ANCHOR', l['text'], l['target'], reason) for l, reason in data['broken_anchors']] +
            [(l['line'], 'EXTERNAL', l['text'], l['target'], '') for l in data['external']]
        )
        file_all_links.sort(key=lambda x: x[0])
        
        if file_all_links or data['case_mismatches']:
            print(f"\nFile: {src} ({len(file_all_links)} links)")
            for line_num, status, text, target, note in file_all_links:
                note_str = f" -- {note}" if note else ""
                print(f"  Line {line_num:4d} | [{status:13s}] | [{text}]({target}){note_str}")
            for l, note in data['case_mismatches']:
                print(f"  Line {l['line']:4d} | [CASE_MISMATCH] | [{l['text']}]({l['target']}) -- {note}")

    print("\n------------------------------------------------------------")
    print(" SUMMARY OF LINK AUDIT")
    print("------------------------------------------------------------")
    print(f"Total Markdown Files Scanned: {len(md_files)}")
    print(f"Total Markdown / HTML Links:  {total_links}")
    print(f"Valid Internal Links:         {valid_links}")
    print(f"External Links:               {len(external_links)}")
    print(f"Broken File Links:            {len(broken_links)}")
    print(f"Broken Heading Anchors:       {len(broken_anchors)}")
    print(f"Case Mismatches:              {len(case_mismatches)}")
    print(f"Code Fence Imbalances:        {len(fence_errors)}")
    print(f"Stale Legacy References:      {len(stale_refs)}")
    print("------------------------------------------------------------\n")

    if broken_links:
        print("[FAIL] BROKEN FILE LINKS:")
        for src, line, text, target, reason in broken_links:
            print(f"  [{src}:{line}] '{text}' -> '{target}' ({reason})")
        print()

    if broken_anchors:
        print("[FAIL] BROKEN HEADING ANCHORS:")
        for src, line, text, target, reason in broken_anchors:
            print(f"  [{src}:{line}] '{text}' -> '{target}' ({reason})")
        print()

    if case_mismatches:
        print("[WARN] CASE MISMATCHES (Works on Windows, fails on Linux/CI):")
        for src, line, target, actual in case_mismatches:
            print(f"  [{src}:{line}] '{target}' -> actual on disk: '{actual}'")
        print()

    if fence_errors:
        print("[FAIL] CODE FENCE ERRORS:")
        for src, errs in fence_errors:
            print(f"  [{src}] {errs}")
        print()

    if stale_refs:
        print("[WARN] STALE LEGACY REFERENCES:")
        for src, line, label, snippet in stale_refs:
            print(f"  [{src}:{line}] {label}: {snippet}")
        print()

    return {
        'total_files': len(md_files),
        'total_links': total_links,
        'valid_links': valid_links,
        'external_links': len(external_links),
        'broken_links': broken_links,
        'broken_anchors': broken_anchors,
        'case_mismatches': case_mismatches,
        'fence_errors': fence_errors,
        'stale_refs': stale_refs,
        'results_by_file': results_by_file
    }

if __name__ == '__main__':
    res = main()
    if res['broken_links'] or res['broken_anchors'] or res['fence_errors']:
        sys.exit(1)
    sys.exit(0)
