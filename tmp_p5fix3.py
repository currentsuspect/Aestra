#!/usr/bin/env python3
"""Update the FloatingPanelSpec use sites left behind by the P5 rename.

floatingPanelSpec() now returns Content::FloatingPanelGeometry, and the local
FloatingPanelSpec alias was removed with the old switch. Four declarations still
name the old type. These are const references to a lookup result, so the fix is
the type name only -- no behaviour, no signature change.
"""
P = '/home/currentsuspect/Dev/Aestra-p2/Source/Core/AestraContent.cpp'
s = open(P, encoding='utf-8').read()

OLD = 'const FloatingPanelSpec& spec = floatingPanelSpec(view);'
NEW = 'const Content::FloatingPanelGeometry& spec = floatingPanelSpec(view);'

n = s.count(OLD)
assert n == 4, f'expected 4 call sites, found {n} -- reading before replacing'
s = s.replace(OLD, NEW)

assert 'FloatingPanelSpec' not in s, 'a FloatingPanelSpec reference survived the rename'
open(P, 'w', encoding='utf-8').write(s)
print(f'updated {n} call sites; no FloatingPanelSpec reference remains')