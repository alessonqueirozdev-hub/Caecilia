# Prompt to resume caecilia in the cloud

Paste this into a new Claude Code session on claude.ai/code opened on this
repository (branch `rust-2.0`):

---

You are resuming work on **caecilia**, which until 2026-11-23 was run from
Alesson's Windows PC. Reply to Alesson in Brazilian Portuguese.

1. Read `docs/handoff/HANDOFF.md` in full: state, open work, decisions,
   traps and the rules that hold everywhere.
2. Run `bash scripts/cloud-setup.sh --test` and tell me what passes and what
   fails, with the output. Do not "fix" a failure by weakening a test.
3. Then tell me in a few lines where things stand and what you propose to do
   next, and wait for my answer before changing code.

Never merge, never force push, never publish to crates.io, and never post
on someone else's PR or issue without my OK in this chat.

---
