# Questions requiring a decision

1. **Where is `mta_neon_source`?** `AGENTS.md` makes it the behavioural reference, but it is not in
   the checkout and the docs point at `/home/samuel/...`. Should it be added as a submodule, a
   documented clone step, or provided locally for the next contribution? Until then, sync changes
   can only be checked against the line ranges already recorded in the docs.

2. **Which server is authorized for device tests?** `README.md` names
   `worldrpgbroficial.duckdns.org:22003` and the San Andreas Online server. Is that the intended QA
   server for this work, and is a second client (desktop or second phone) available for two-player
   tests?

3. **Should the 0.35 blend for server ped observers be replaced now?** `UpdateTargetPosition` is
   wired for keysync remote players but not for server peds (`AGENTS.md` row 40). The roadmap
   (5.3) says replace it, not run both. This needs a device to tell whether it is the cause of the
   "peds do not move" bug.

4. **Is `libc++_shared.so` / 16 KB alignment still an open issue?** The build already uses the
   static STL and 16 KB LOAD alignment. The README still says the repo audit fails. Should the row
   be closed after an APK audit, or is there a second library still failing?

5. **Branch/PR workflow.** This work is committed on `staging-stage-5` only, per
   `docs/sync-port/MASTER_PLAN.md`. Do you want it pushed and opened as a PR, or kept local for
   review? Nothing was pushed.

6. **Doc cleanup scope.** The audit lists several README/roadmap rows contradicted by the code. May
   a follow-up commit correct them in place, or should each be a separate PR?
