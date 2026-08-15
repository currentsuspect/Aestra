## Release: __VERSION__

The release tag was just cut. One manual step remains: compose and publish the
site changelog on aestra.studio (the draft below is the material, not the final
copy — vet the notes, then paste).

### Checklist

- [ ] Run `npm run changelog:draft` in `~/Dev/Aestra-website` (fresh draft)
- [ ] Compose the new entry: create `src/content/changelog/010-__VERSION__.md`
      with frontmatter (`version`, `date`, `status: released`, `order`) and the
      producer-note bullets; renumber the older released files
- [ ] Reset `src/content/changelog/000-unreleased.md` to a fresh active entry
- [ ] Push to `main` (Vercel deploys automatically) and verify
      https://aestra.studio/changelog shows the entry
- [ ] Update `RELEASES.md` in the Aestra repo if the milestone entry is missing

### Draft (generated at tag time)

```
__DRAFT__
```

> The "uncovered" list at the end of the draft are feat/fix PRs without a
> producer note — decide per PR whether any deserve a manual entry.
