# Adventure

Use this to run a generative text-adventure game: an interactive fiction session invented on
the fly, with the user playing a character and choosing actions each turn. Use `TOOL:ASK` to
collect the player's action at every turn — never assume or invent what they chose to do.

## Setup (first turn only)

- Establish a premise in a few sentences: setting, tone (comic, eerie, pulp-adventure,
  whatever fits what the user asked for or seems to want), and the opening scene the player
  finds themselves in.
- Establish the player's starting situation concretely: where they are, what they can
  immediately perceive, and what they're carrying, if anything. Don't over-explain the whole
  world up front — reveal it through play.
- End the setup with `TOOL:ASK` for their first action, phrased open-ended ("What do you do?"),
  not as a multiple-choice menu unless the user specifically wants a menu-driven style.

## Per-turn loop

1. Read the player's stated action from the `TOOL:ASK` response.
2. Resolve it against the current state (location, inventory, flags, NPCs present) — the
   outcome should follow from what's actually plausible given the established scene, not from
   what would make the best story regardless of consistency.
3. Narrate the result in a short paragraph — consequence first, then any new details the scene
   reveals. Keep pacing brisk; a wall of text before the next choice kills momentum.
4. `TOOL:ASK` again for the next action.

## Maintaining state

Track this explicitly across turns (in your own working notes, not necessarily shown to the
player) so the world stays consistent:

- **Location** — where the player currently is, and what connects to it.
- **Inventory** — what they're carrying; only let them use items they've actually picked up.
- **Flags** — anything that's happened that should be remembered (a door unlocked, an NPC
  angered, a clue found) so later scenes react to earlier choices instead of ignoring them.
- **NPC state** — if a character was left alive, allied, or angered, keep that consistent
  rather than resetting them to a neutral default next time they appear.

Never silently contradict established state — if the player already dropped the lantern two
turns ago, they can't use it now without picking it back up. If you're not sure what's
established, treat it as not-yet-decided and decide it in a way consistent with everything
that *is* established, rather than guessing against your own earlier narration.

## Player agency and pacing

- Accept genuinely unexpected actions rather than steering the player back onto a rail — if
  they try something odd, resolve it plausibly (it might fail, it might work in a way that
  surprises everyone) rather than blocking it with "you can't do that" unless it's truly
  impossible given the fiction.
- Let failure be real — not every action should succeed. A locked door that doesn't open, a
  risky jump that doesn't land, matters more to a story than universal success.
- Vary scene length and tension rather than every turn being the same size beat — a tense
  moment can resolve in one line; a new location deserves more room.
- If the story is heading toward a natural ending (goal achieved, character dies, mystery
  solved), let it end — don't artificially extend a finished story just to keep the loop going.

## What NOT to do

- Don't invent the player's action for them — always resolve through `TOOL:ASK`, never
  narrate what they "decide" to do on their behalf.
- Don't contradict previously established facts about the world, inventory, or NPCs.
- Don't front-load a huge wall of lore before the first real choice — establish just enough to
  act, let the rest emerge through play.
- Don't turn every scene into a puzzle with one correct action — most turns should have several
  reasonable options that all lead somewhere.
