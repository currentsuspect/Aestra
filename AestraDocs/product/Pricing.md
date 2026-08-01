# Aestra Pricing & Offer System

**Status:** Internal — approved direction for public beta
**Last Updated:** 2026-08-01
**Owner:** Dylan

---

## Pricing Philosophy

Aestra is free. Revenue comes from making users better, not from restricting access. The pricing model is closer to Patreon/Fortnite than Adobe/Ableton.

**Key principles:**
- Free users can make a professional album. No watermarks on audio exports.
- Paying supporters get optional creative tools and a closer product relationship, not DAW feature gates.
- The psychological frame is "support the craft" not "pay for software."
- Recurring infrastructure is never sold as a perpetual entitlement.
- Muse is local-first. A future hosted or training service is a separate, opt-in decision with its own economics and privacy review.

---

## Tier Matrix

### Core (Free)

| Aspect | Detail |
|--------|--------|
| Price | $0 |
| Digital identity | Standard Aestra account |
| DAW Features | Everything. Full routing, plugin hosting, recording, export, audition, version control |
| Plugins | Basic built-in plugins (EQ, compressor, reverb, delay) |
| Collaboration | Join and edit projects when invited, when available |
| Cloud | No owned workspace or storage allowance |
| Muse | Not included |
| Community | Public channels |

**Psychological frame:** "You belong here. The DAW is complete."

---

### Supporter ($5/mo or $50/yr)

| Aspect | Detail |
|--------|--------|
| Price | $5/month or $50/year (save $10) |
| DAW Features | Same as Core — no feature gates |
| Plugins | Native Suite catalogue while the subscription is active |
| Releases | New Native Suite releases when they are ready; no monthly or quarterly cadence promise |
| Muse | Local-on-device Muse when ready |
| Collaboration | Create and own shared workspaces when available; invited Core users participate without subscribing |
| Included storage | 10 GB for shared projects |
| Additional storage | Priced separately |
| Community | Development updates and a Supporter feedback channel |

**Psychological frame:** "You support the craft. You get a creative partner."

**Retention mechanics:**
- The Native Suite catalogue gains value as useful plugins ship
- Local Muse adds value without recurring inference cost or cloud dependency
- Development access and feedback create relationship value
- Retention must come from product usefulness, not an artificial release cadence

---

### Founder ($129 one-time, limited to 500)

| Aspect | Detail |
|--------|--------|
| Price | $129 one-time, maximum 500 sales |
| DAW Features | Same as Core |
| Founder Collection | Fixed launch bundle, owned permanently |
| Supporter | Included for 24 months from public beta |
| After 24 months | Permanent 25% discount on Supporter renewal |
| Founder card | Numbered digital record, permanent |
| Credits | Optional name in Founding Supporters credits |
| Physical goods | None; the offer is fully digital |
| Collaboration | Included through the 24-month Supporter period; renewable afterward at the Founder discount |
| Cloud | No lifetime storage or perpetual usage entitlement |

**Psychological frame:** "You believed first. This is your legacy."

**Scarcity mechanics:**
- Hard cap of 500 completed purchases
- Numbered digital cards ("Founder #0042")
- Sales open at public beta; the waitlist sends notice and does not reserve a number
- The Founder Collection is defined before sale rather than expanding into every future release

**Why $129 works:**
- A capped digital offer produces at most $64,500 gross launch capital with no fulfillment inventory.
- Twenty-four months of Supporter has a $100 annual-plan list value, leaving a bounded premium for the permanent collection, digital record, and early backing.
- The model avoids an open-ended storage, inference, support, or release obligation.
- Forecast net receipts after payment fees, taxes, refunds, and regional pricing; gross sales are not margin.

### Collaboration and additional storage

Collaboration is a future Supporter benefit. A Supporter creates and owns the shared workspace; invited Core users can join and edit without subscribing. Each active Supporter includes 10 GB of shared-project storage. Storage is charged to the workspace owner rather than every collaborator, and additional storage is priced separately.

If Supporter ends, only the cloud workspace becomes read-only. Local projects remain editable and exportable with Core. The cloud workspace provides at least 30 days to download, transfer ownership, or reactivate, with repeated warnings before deletion.

This model still requires storage caps, lifecycle rules, abuse controls, and measured unit economics. Backblaze B2 is a leading storage candidate, but provider pricing does not make a perpetual customer promise sustainable. No Founder copy may imply lifetime storage.

The enforceable workspace state machine, roles, quota accounting, history safeguards, and launch tests live in [Collaboration-Entitlements.md](./Collaboration-Entitlements.md).

---

## Card System Design

### What Cards Are

The Founder card is a collectible digital record tied to an Aestra account. It represents early backing, not software access or product status.

### Card Properties

| Property | Description |
|----------|-------------|
| Offer | Founder |
| Format | Digital only |
| Number | Unique ID, for example #0042 |
| Ownership | Permanent account record |
| Public display | Opt-in only |

### Card Display Locations

| Location | How Card Shows |
|----------|---------------|
| Account | Full digital card and number |
| App credits | Optional Founding Supporters credit |
| Community profile | Optional display if profiles ship |

Do not add paid rarity, random rewards, or engagement mechanics without a separate product review. The card should remain a quiet record of early backing.

---

## Revenue Streams

| Stream | Type | Target |
|--------|------|--------|
| Supporter subscriptions | Recurring | Primary revenue |
| Founder sales | One-time (limited) | Upfront capital, community seeding |
| Individual plugin purchases | One-time | Optional ownership outside Supporter |
| Extra collaboration storage | Recurring/usage-backed | Separate add-on only after unit economics are proven |

### Not Revenue Streams (Important)

These are deliberately NOT monetized:
- DAW features — never gated
- Export quality — never watermarked (audio)
- Track count — never limited
- Plugin hosting — never restricted
- Basic plugins — always free
- Lifetime cloud storage — never promised
- Hosted Muse inference — not bundled into a local-Muse promise

---

## Financial operating model

Do not forecast from gross conversion alone. The launch model must track:

- free-to-Supporter conversion by cohort
- monthly and annual Supporter churn
- net revenue after payment fees, taxes, refunds, and regional pricing
- plugin production and support cost
- Founder attachment and post-included-period renewal
- cloud storage, operation, egress, abuse, and support cost per active user before pricing cloud

Founder gross is capped at $64,500. Treat it as launch capital, not recurring revenue. Supporter is the primary business; the Native Suite, local Muse, and collaboration workflow are the places to lean. The included 10 GB allowance and any extra-storage pricing must be validated against observed unit economics before collaboration launches.

### Regional Pricing Consideration

$5/mo is impulse in the US/EU but significant in Kenya, India, Brazil, Southeast Asia. Consider:
- Regional pricing for Supporter tier (e.g., $2-3/mo in emerging markets)
- Founder is global flat rate, while regional affordability is handled through Supporter

---

## Competitor Price Comparison

| Product | Entry Price | Full Price | Aestra Equivalent |
|---------|-----------|-----------|-------------------|
| FL Studio Fruity | $99 | $499 (All Plugins) | Free (Core) |
| Ableton Live Intro | $99 | $749 (Suite) | Free (Core) |
| Pro Tools | $99/yr | $349/yr | Free (Core) |
| Logic Pro | $199 | $199 | Free (Core) |
| Bitwig | $99 | $169/yr | Free (Core) |
| REAPER | $60 | $60 | Free (Core) |
| Spotify Premium | $10.99/mo | $10.99/mo | $5/mo (Supporter) |

Aestra Core is free. Aestra Supporter is cheaper than Spotify. That's the positioning.

---

*This document is internal. Pricing may change before public launch.*
