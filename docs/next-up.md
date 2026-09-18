# Next up

**Active work only.** Rewritten constantly, kept tiny, and an item is *removed* when it
lands or is dropped — never ticked off in place. Everything else lives in
GitHub issues (work for later) or `docs/reasoning/` (why things are the way they are).
If a fact wants to survive, it does not belong in this file.

Last updated 2026-09-18.

## Now

**This repository was re-based on Strux on 2026-09-18 and holds nothing of its own yet.**
The 218 tracked files of [Strux](https://github.com/vanBassum/Strux) were copied in from
`c:\Workspace\Strux` at its working-tree state (commit `03a6817` plus 27 files of
uncommitted framework work that had been built green that morning), and the project
identity was renamed: `project(Lablr)`, `PROJECT_NAME: "Lablr"` in the release workflow,
and `DEV_HOST`/`GITHUB_REPO`/`PRODUCT_NAME` in `frontend/src/config.ts`. `main/strux/` was
deliberately left untouched — it is the template's, and renaming it would cost the ability
to trade improvements with upstream. So what builds here today is the Strux demo:
`app/LedManager` and its home page, and nothing else.

**Outstanding: the previous Lablr is only in git history.** Everything before this — the
C# render/print API, the label config tree, the web UI — was deleted from the working tree
before the copy and is still reachable at commit `f0679f0`. Nothing has been committed
since, so the pivot is not yet a decision the repository records.

**Outstanding: no board, no product code.** The boards that came with the template are
`esp32_devkit` and `esp32c3_supermini`; if this is to be the phone→BLE→USB→Dymo bridge
that an earlier `lablr-bridge` scaffold aimed at, it wants an ESP32-S3 board folder and
two managers of its own. That is a decision, not a task — it is not started.

**Outstanding: not built here.** The copy has not been through `idf.py set-target` /
`build` in this directory, and the frontend has not been `pnpm install`ed. It compiled in
Strux minutes before the copy, which is evidence and not the same thing.
