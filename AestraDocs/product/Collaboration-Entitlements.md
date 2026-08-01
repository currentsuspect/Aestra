# Collaboration Entitlements

**Status:** Internal product contract — implement before launch
**Last Updated:** 2026-08-01
**Owner:** Dylan

---

## Product promise

> Supporter funds Aestra and receives the Native Suite, local Muse capabilities, asynchronous versioned collaboration, cloud workspace storage, and a closer product relationship. The core DAW and access to your own music remain free.

Collaboration v1 is asynchronous project collaboration through Takes. It is not simultaneous DAW control, shared transport, remote recording, live audio streaming, or real-time co-editing.

The subscription governs the shared cloud workspace. It never governs a user's local project or music. Any downloaded project remains an ordinary Aestra project that Core can open, edit, duplicate, and export without a subscription.

## Three separate concepts

| Concept | Meaning |
|---------|---------|
| Workspace owner | Controls billing, quota, workspace deletion, and ownership transfer. An active collaborative workspace must be owned by an active Supporter. |
| Project role | Owner, editor, or viewer. This controls workspace permissions, not billing eligibility. |
| Supporter entitlement | Allows an account to create or own active collaborative workspaces. Invitees do not need Supporter to participate. |

An invited Core user can be an editor or viewer. Do not require every band member to subscribe.

## Workspace state machine

| State | Cloud behavior | Allowed transitions |
|-------|----------------|---------------------|
| Active | Full Takes collaboration, uploads, invitations, ownership transfer, and quota use. Editors may add revisions; viewers may inspect and download. | Lapsed when the owner's paid period ends; Deleted by an explicit owner deletion request under the deletion policy. |
| Lapsed | Cloud workspace becomes read-only. Existing members may inspect history, download project data, and export available artifacts. No uploads, new Takes, invitations, or cloud-history changes. A clear deletion date is calculated and the first warning is sent. | Active by renewal; Active by accepted transfer to another eligible Supporter; Grace after the lapsed notice stage. |
| Grace | Cloud remains read-only. Show the deletion date in product, send repeated warnings, and keep renewal, download, and ownership transfer available. Grace lasts at least 30 days from the first deletion notice. | Active by renewal or accepted transfer; Deleted after the published date. |
| Deleted | Cloud blobs and retained Takes are removed according to the published deletion policy. Only the minimum billing, security, and deletion audit records required by law or fraud controls may remain. | No restoration promise. A local copy may be uploaded later as a new workspace by an eligible Supporter. |

Local copies remain editable and exportable in every state. A cloud-state transition must never lock, alter, or remotely delete a local project.

## Roles and permissions

| Capability | Owner | Editor | Viewer |
|------------|-------|--------|--------|
| View and download | Yes | Yes | Yes |
| Create Takes and upload revisions while Active | Yes | Yes | No |
| Invite or remove members | Yes | No | No |
| Change roles | Yes | No | No |
| View quota usage | Yes | Optional project usage | No billing detail |
| Buy additional storage | Yes | No | No |
| Transfer or delete workspace | Yes | No | No |

Ownership transfer requires explicit acceptance by another active Supporter with sufficient quota. The recipient sees the storage impact before accepting. Transfer moves billing, quota, deletion authority, and workspace ownership together.

## Quota contract

- An active Supporter receives 10 GB total across all cloud workspaces they own, not 10 GB per workspace.
- Project assets, retained Takes, generated previews, and trash awaiting permanent deletion count toward the quota.
- Storage is charged to the workspace owner, including files uploaded by editors.
- Identical content-addressed blobs count once per owner. System-generated snapshots must reference existing blobs rather than silently duplicating whole projects.
- Show total usage, per-workspace usage, version-history usage, trash usage, and the effect of deleting or compacting data.
- Warn at meaningful thresholds before the quota is full. Reaching the limit blocks new cloud uploads and revisions, never local editing, opening, duplication, or export.
- Additional storage is a separately priced add-on. Do not silently overage-bill.

## Version-history safeguards

Normal collaboration must not consume unpredictable storage merely because the system creates history.

- Provide visible retention and compaction controls before launch.
- Automatically compact redundant system checkpoints where content is already represented by a retained Take.
- Never remove named Takes, branch heads, conflict evidence, or the last known-good revision without explicit user confirmation or a clearly published retention rule.
- Preview the bytes reclaimed before destructive compaction.
- Keep quota accounting deterministic: the same retained graph and assets must produce the same displayed usage.

## Conflict and durability rules

- Every cloud revision is immutable and attributable to an account and parent Take.
- Concurrent edits create visible branches or an explicit conflict; last-writer-wins is not acceptable for project state.
- Upload completion must be durable before the client reports success.
- Download/export must include enough project and asset data to reopen the work locally without collaboration services.
- Rendered compatibility artifacts may supplement plugin state but must not replace the original editable project data.

## Privacy and safety gates

Before launch, publish the storage provider and subprocessors, retention and deletion periods, encryption behavior, invite-token security, access revocation, abuse limits, and incident-response path. Workspace access must be least-privilege and auditable. Training or hosted Muse use of workspace data is not implied and requires a separate explicit opt-in.

## Launch acceptance

Collaboration cannot ship until tests prove:

- every state transition and permission in this document
- Core invitees can participate without becoming Supporters
- lapse affects only the cloud workspace
- local open, edit, duplicate, audio export, and stem export remain available after lapse and deletion
- quota usage is visible, deterministic, and bounded under repeated automatic Takes
- ownership transfer preserves history and permissions atomically
- a complete workspace export can be reopened offline

Any public pricing or terms change that conflicts with this document requires this contract to change in the same review.
