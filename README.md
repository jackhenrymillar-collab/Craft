# Foundry

Physical-modelling percussion plugin (JUCE, C++20). Every sound comes from a simulated object,
no samples. Working name; see `CLAUDE.md` for the full brief and `docs/` for build notes,
decisions and design notes.

Status: **M3 + low end**, one pad that can be a drum head (on a hoop or a kick shell), bar, plate, bowl/bell, tube, spring or box in
any of the table's materials, optionally bolted or rubber-mounted to a second part, with snare
wires or sizzle rivets, rice / beads / sand / chain resting on it, hit by a mallet, hammer, hand,
kick beater or dropped ball or played with a brush or scrape stick, with a pitch drop after
hard hits on drum heads, heard through a contact mic and an air
mic, plus the Age / Deform / Twist / Puncture treatments and an optional 2x oversampling.
Formats: AU, VST3, Standalone (macOS first).

Render demos: `scripts/render_demos.sh build renders` (see `render_cli --help` for options).

- Build: [`docs/BUILD.md`](docs/BUILD.md)
- Decisions: [`docs/DECISIONS.md`](docs/DECISIONS.md)
- Treatments: [`docs/TREATMENTS.md`](docs/TREATMENTS.md)

Licence: not decided yet (JUCE is dual-licensed; see the decisions log).
