# Multi-Track Music

# Multi-Track Music

Build, mix, and play layered music compositions using `TOOL:MUSIC`. Each project holds multiple independent tracks that are mixed together at play time — each with its own volume, pan, and note script.

## When to Use

- User asks to compose, arrange, or layer music
- User wants to build a multi-part piece (bass + melody + drums, etc.)
- User wants to adjust mix levels (volume/pan) on individual tracks
- User wants to save and reload a composition

## Commands

| Command | Syntax | Description |
|---------|--------|-------------|
| `create` | `create <name>` | Create a new empty project |
| `load` | `load <name>` | Load a saved project by name |
| `list` | `list` | List all saved projects |
| `add` | `add <track_name> <script>` | Add a track with a note script |
| `set` | `set <n> <field> <value>` | Modify track n's volume, pan, or script |
| `remove` | `remove <n>` | Remove track n (1-based index) |
| `tempo` | `tempo <bpm>` | Set project tempo (20–300) |
| `play` | `play` | Mix and play all tracks simultaneously |
| `show` | `show` | Display current project details |
| `save` | `save` | Persist the current project to disk |
| `clear` | `clear` | Remove all tracks from the current project |

## Track Fields

| Field | Range | Default | Notes |
|-------|-------|---------|-------|
| `volume` | 0–100 | 80 | Relative loudness in the mix |
| `pan` | -100 to +100 | 0 | Negative = left, positive = right |
| `script` | — | — | Note tokens (same format as TOOL:PLAY) |

## Script Format

Track scripts use the same note tokens as `TOOL:PLAY`:

- **Notes:** `C4`, `D#5`, `E-3` (letter + optional accidental + octave)
- **Duration:** `C4:0.5` (seconds)
- **Chords:** `[C4 E4 G4]` (simultaneous notes)
- **Tempo:** `T120` (BPM override for this track)
- **Volume:** `V70` (track-level volume override)
- **Pause:** `P1` (1 beat of silence)
- **Note length:** `L4` (default note length in beats)

## Typical Workflow

```
TOOL:MUSIC create my_song
TOOL:MUSIC tempo 128
TOOL:MUSIC add bass C2:0.5 E2:0.5 G2:0.5 C3:0.5
TOOL:MUSIC add melody C4:0.25 D4:0.25 E4:0.25 F4:0.25 G4:0.25
TOOL:MUSIC add pad [C4 E4 G4]:2.0 [F4 A4 C5]:2.0
TOOL:MUSIC set 1 volume 60
TOOL:MUSIC set 1 pan -30
TOOL:MUSIC set 3 volume 40
TOOL:MUSIC set 3 pan 20
TOOL:MUSIC show
TOOL:MUSIC play
TOOL:MUSIC save
```

## Tips for the Model

- **Start with `create`** before adding tracks — there must be an active project.
- **Use `show`** after building tracks to verify the arrangement before playing.
- **Balance the mix:** bass/low tracks typically 50–70 vol, melody 80–100, pads/atmosphere 30–50.
- **Pan for width:** spread tracks left/right to create stereo image.
- **Tempo is project-level** — set it once with `tempo`, individual tracks inherit it unless they specify `T<BPM>` in their script.
- **`play` mixes all tracks simultaneously** — the result is a layered composition, not sequential playback.
- **Save early, save often** — `save` persists to `.hali/projects/<name>.json`. Use `load` to resume later.
- **`clear`** wipes all tracks but keeps the project name and tempo.

## Example: 3-Track Arrangement

A simple waltz in C major:

```
TOOL:MUSIC create waltz
TOOL:MUSIC tempo 90
TOOL:MUSIC add bass C2:1.0 G2:1.0 E2:1.0 C2:1.0
TOOL:MUSIC add chords [C4 E4 G4]:1.0 [G4 B4 D5]:1.0 [A4 C5 E5]:1.0 [F4 A4 C5]:1.0
TOOL:MUSIC add melody E5:0.5 D5:0.5 C5:0.5 D5:0.5 E5:0.5 D5:0.5 C5:1.0
TOOL:MUSIC set 1 volume 55
TOOL:MUSIC set 2 volume 45
TOOL:MUSIC set 2 pan -20
TOOL:MUSIC set 3 volume 85
TOOL:MUSIC set 3 pan 15
TOOL:MUSIC play
```

## Storage

Projects are saved as JSON in `.hali/projects/`:

```json
{
  "name": "waltz",
  "tempo": 90,
  "tracks": [
    {"name": "bass", "script": "C2:1.0 G2:1.0 E2:1.0 C2:1.0", "volume": 55, "pan": 0},
    {"name": "chords", "script": "[C4 E4 G4]:1.0 ...", "volume": 45, "pan": -20},
    {"name": "melody", "script": "E5:0.5 D5:0.5 ...", "volume": 85, "pan": 15}
  ]
}
```

## Limitations

- `play` is blocking — it plays the full mix before returning.
- Track scripts are validated on `add` and `set`, not on `play`.
- There is no undo — use `show` to check state before making changes.
- Maximum practical track count is limited by audio buffer memory (16+ tracks should be fine).