# Foundry

Physical-modelling percussion plugin (JUCE, C++20). Every sound comes from a simulated object,
no samples. Working name; see `CLAUDE.md` for the full brief and `docs/` for build notes,
decisions and design notes.

Status: **M2**, one pad that can be a drum head, bar, plate, bowl/bell, tube, spring or box in
any of the table's materials, hit by a mallet, hammer, hand or dropped ball, heard through a
contact mic and an air mic, plus the Age / Deform / Twist / Puncture treatments.
Formats: AU, VST3, Standalone (macOS first).

Render demos: `scripts/render_demos.sh build renders` (see `render_cli --help` for options).

- Build: [`docs/BUILD.md`](docs/BUILD.md)
- Decisions: [`docs/DECISIONS.md`](docs/DECISIONS.md)
- Treatments: [`docs/TREATMENTS.md`](docs/TREATMENTS.md)

Licence: not decided yet (JUCE is dual-licensed; see the decisions log).
