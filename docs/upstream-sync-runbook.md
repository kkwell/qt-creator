# Remote Update Lock and Upstream Sync Runbook

## Current policy

The current local `embed-labs` checkout is the authoritative product source.
Remote synchronization is locked.

Without an explicit user request, do not run:

- `git fetch`
- `git pull`
- `git merge`
- `git rebase`
- submodule updates that import new remote commits
- any push or pull-request operation

The repository may contain remote-tracking references from an earlier audit.
They are not authorization to update the worktree.

## Branch and publication policy

- Stay on `embed-labs` for all modifications.
- Do not create or switch to feature, integration, or release branches.
- Commit completed major features locally after their acceptance gates pass.
- Do not push local commits.
- Stage named files only; do not accidentally include the untracked
  `AGENTS.md` or unrelated user changes.

## Procedure only after explicit authorization

If the user explicitly asks for a remote update, first restate the requested
remote and target commit or branch. Then:

```text
verify active branch is embed-labs
→ verify worktree and list every uncommitted user change
→ record current local HEAD and product test evidence
→ create a local annotated safety tag if the user allows it
→ fetch only the explicitly requested remote refs
→ compare commits and submodule changes without modifying the worktree
→ report expected conflicts and API changes
→ obtain confirmation if the requested merge scope is ambiguous
→ merge on embed-labs without publishing
→ resolve only update-related conflicts
→ rebuild the product plugin allow-list
→ run Qt Creator and EtherCAT regression gates
→ update baseline, delta, and compatibility documentation
→ create a local merge commit
→ do not push
```

This procedure does not grant standing authorization. Each later remote import
requires a new explicit request.

## Failed or unsafe update

If the authorized update cannot be completed safely:

- Stop before overwriting unrelated work.
- Preserve the pre-update local commit.
- Record the exact conflict or tool failure.
- Do not replace the product with a clean upstream checkout.
- Do not force-push, reset hard, or delete local product history.

## Upstream delta objective

When a future update is authorized, the maintenance objective is to keep
EtherCAT behavior in product-owned plugins and reduce direct upstream Core
patches. An update must not be used as an excuse to rewrite working local
product functionality without a separate issue and evidence.
