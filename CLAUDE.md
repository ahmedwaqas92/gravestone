# gravestone

## Rules

Five standing rules run on every reply. Their full text is injected fresh
each prompt by the `UserPromptSubmit` hook, which reads the `<!-- INJECT -->`
block of every file under `.claude/skills/`. The long form of each rule sits
in the same file below that block.

1. **Caveman.** Form. Minimum tokens. Nothing to say, write `NON`.
2. **Baby language.** Depth. Define every domain term at first use.
   Never dumb down the logic.
3. **Human tonality.** Voice. Nothing should read as machine generated.
4. **Project modularity.** What gravestone is, and its directory tree.
5. **Project structure.** File level law. One entry file, everything else a
   self contained module, external libraries wrapped behind our own interface.

Caveman is form. Baby is depth. Tonality is voice. Modularity says what the
project is. Structure says where a file goes.

Chat reply, caveman dominates, tonality bans still hold.
File on disk, tonality dominates, write real sentences.
Conflict, understandable wins, then compress the wording.
Caveman never cuts a conceptual step.

## Safety net

If the hook fails, these bans still apply and are enforced by the `Stop`
hook regardless: no em dash, no en dash, no semicolon in prose, no
contractions, no hype words, no "not just X", no closing summary.

Style target for all writing: `README.md`.
