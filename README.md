# H · A · L · I | Haliastur | Hardware-Aware Local Inference

**A standalone, agentic LLM shell for your terminal.**

Hali is a local-first agentic coding/chat shell built on [llama.cpp](https://github.com/ggml-org/llama.cpp), rendered with [notcurses](https://github.com/dankamongmen/notcurses). No server, no browser tab, no cloud dependency — just a fast terminal UI driving a local model with tool use, RAG, MCP, Web development server and careful context management.

<img width="831" height="771" alt="Screenshot From 2026-09-26 07-42-59" src="https://github.com/user-attachments/assets/105b2376-d5c0-48b9-b0d6-6922a758e6cc" />

![made with badge](.hali/badge.svg)
![unit tests](.hali/test_badge.svg)

## Why

Most agentic shells assume a hosted API and treat context as free. Hali assumes neither: it's built for consumer GPUs (developed against a 16GB RTX 5060 Ti) where the KV cache is a scarce resource and every tool call has to earn its place in the context window.

## Features

- **notcurses TUI** — plane-based rendering, modal popups, persistent input history (Up/Down navigation), live `/set` commands for generation parameters, in-chat search, Kitty keyboard protocol support for reliable input in modern terminals, and mouse scroll-wheel support.
- **Theme system** — three built-in themes (Dark, Light, Navy) switchable at runtime with `/theme` or F12.
- **Fragmentation-aware KV cache management** — a `KVCachePreset` enum (`F16` / `Balanced` / `Compact`) coupled to flash-attention settings, with graceful recovery when sequence removal fragments the cache.
- **Dynamic tool-result budgeting** — `max_tool_result_size()` targets ~75% of remaining context so a single large tool result can't blow the budget.
- **Unambiguous tool-call protocol** — an explicit `HALI_END_TOOL` terminator so tool boundaries never get confused with model chatter.
- **Multi-Token Prediction (MTP)** — optional speculative decoding via MTP-capable GGUF models. Configurable with `mtp_enabled`, `mtp_n_max`, `mtp_n_min`, `mtp_p_min`, `mtp_adaptive`, and `mtp_pos_threshold` settings to control how many draft tokens are proposed, the acceptance threshold, and adaptive self-tuning.
- **Suffix drafting** — a lightweight n-gram `SuffixDrafter` that matches recent tokens against earlier context and drafts continuations, helping with repetitive/structured text when MTP acceptance is low.
- **GPU introspection** — `gpu_info` detects VRAM via the ggml backend API and scales the KV-cache restart threshold (8 GB → 75 %, 16 GB → 85 %, 32 GB → 90 %, cap 92 %).
- **Usage logging** — per-turn metrics (LLM latency, completion tokens, tool calls/errors, KV usage, files read/written) tracked in a `UsageLog` with JSON export and KV-trend detection.
- **Audio system** — polyphonic playback via a note-script parser (`TOOL:PLAY`). Supports sine tones, chords, tempo/volume/length control tokens, and optional UI sounds (tool-completion tick, user-prompt ding). Enable with `audio_enabled` in config or `/audio`.
- **Planning tool** — `TOOL:PLAN` provides structured goal/step tracking (`set`, `add`, `advance`, `skip`) so the model can maintain a visible work plan across turns. Plans are referenced in `SESSION.md` for session handoff.
- **Codebase audit** — `/audit` scans C/C++ source files for issues that make the codebase difficult for an LLM to handle (e.g. missing includes, implicit declarations, overly long functions). Results are scored 0–100, formatted as a table, and fed back to the model as a remediation prompt. Confirmed false positives are persisted to `audit_feedback.json` and filtered from future scans.
- **Command completions** — tab-completion for slash commands in the TUI input line.
- **A full sandboxed tool suite** — every file operation is scoped to a sandbox root (the directory Hali is launched in, or an explicit path argument):

  | Tool                                                    | Purpose                                                                                                                                                                                                                                                |
  |---------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
  | `TOOL:LIST`, `TOOL:READ`, `TOOL:EXISTS`                 | Inspect files inside the sandbox (read access also extends to `extra_dirs`)                                                                                                                                                                           |
  | `TOOL:SEARCH`                                           | Search file contents by pattern with rich flags: `--recursive`, `--line-numbers`, `--count`, `--line-count`, `--files-only`, `--name` (match filenames), `--include=<glob>`, `--context=<n>` (surrounding lines), `--lines=<start>,<end>` (line range) |
  | `TOOL:WRITE`, `TOOL:APPEND`, `TOOL:PATCH`, `TOOL:MKDIR` | Modify files inside the sandbox                                                                                                                                                                                                                        |
  | `TOOL:REPLACE`                                          | Replace text in a file with `--all` and `--count` flags                                                                                                                                                                                                |
  | `TOOL:RUN`                                              | Run a program inside the sandbox, gated by an optional `run_allowed` allowlist and shell-metacharacter detection                                                                                                                                       |
  | `TOOL:BUILD`                                            | Auto-detect and run project build systems (CMake, Gradle, Maven, Python) with modes: `setup`, `build`, `test`, `clean`, `lint` (clang-tidy)                                                                                                                                 |
  | `TOOL:CURL`                                             | HTTP GET via libcurl, with HTML-to-text stripping and a 32 KB response size cap                                                                                                                                                                        |
  | `TOOL:RAG`                                              | Query the local RAG index for extra context                                                                                                                                                                                                            |
  | `TOOL:ASK`                                              | Ask the user directly, for interactive/agentic flows                                                                                                                                                                                                   |
  | `TOOL:GRAPH`                                            | Render bar charts and tree diagrams as ASCII art from a small JSON schema                                                                                                                                                                              |
  | `TOOL:TABLE`                                            | Format pipe-delimited rows into an aligned text table                                                                                                                                                                                                  |
  | `TOOL:MCP`                                              | Invoke a tool on a connected MCP server                                                                                                                                                                                                                |
  | `TOOL:PERMISSION`                                       | Explicit user confirmation before destructive or irreversible actions                                                                                                                                                                                  |
  | `TOOL:RESTART`                                          | Hand off to a fresh session, writing current task context to `SESSION.md` first                                                                                                                                                                        |
  | `TOOL:PLAY`                                             | Play a melody from a note-script (sine tones, chords, tempo/volume/length control)                                                                                                                             |
  | `TOOL:PLAN`                                             | Structured goal/step planning: `set <goal>`, `add <step>`, `advance`, `skip <idx>`                                                                                                                             |
  | `TOOL:CHOICE`                                           | Multiple-choice popup: `TOOL:CHOICE "question" "opt1|opt2|opt3"`                                                                                                                                              |
  | `TOOL:STAT`                                             | File metadata: size, type, permissions, line/word/char count                                                                                                                                                   |
  | `TOOL:SYSINFO`                                          | System info: user, uid, hostname, OS, kernel, arch, cwd                                                                                                                                                        |
  | `TOOL:GIT`                                              | Run whitelisted git subcommands (`status`, `log`, `diff`, `add`, `commit`, `branch`, `show`) in the sandbox                                                                                                   |
  | `TOOL:HEX`                                              | xxd-style hex dump with optional offset and length, capped at 256 lines                                                                                                                                        |
  | `TOOL:TUNE`                                             | Adjust sampling settings live: `temperature`, `top_p`, `min_p`, `top_k`, `penalty_repeat`, `penalty_freq`, `penalty_present`, `penalty_last_n` (values are clamped to valid ranges) |
  | `TOOL:DATE`, `TOOL:TIME`, `TOOL:RND`, `TOOL:INTROSPECT` | Small utility/introspection tools                                                                                                                                                                                                                      |

- **Persistent run-allowlist** — user-approved `TOOL:RUN` program names are saved to `.hali/user-approved.txt` under the sandbox with timestamps and a 30-day TTL, so approvals survive across sessions without re-prompting. Revoke individual approvals with `/revoke <name>`.
- **MCP client** — connect to external Model Context Protocol servers (e.g. JetBrains IDE built-in MCP servers) with `--mcp`, filter which tools get exposed with `--mcp-filter`, and dry-run the resulting system context with `--mcp-test`. Server connection details live in `mcp.json`.
- **Skills system** — load one or more markdown skill files into the static system-prompt prefix at session start with `--skill <name>` (no per-turn routing, so it doesn't disrupt the KV cache). `hali.md`, `persona.md`, and `AGENTS.md` are auto-discovered from the current directory if present. Skills can also be loaded mid-conversation with `/skill <name> [kickoff]`. The [`skills/`](skills/) folder ships a starter set covering debugging, TDD, code review, CMake/build troubleshooting, memory-safety review, dependency-free frontend work, SmallBASIC's raylib plugin, free JSON API sourcing, a local-info feed pattern, planning, spiking, code simplification, house style, skill import, and a few just-for-fun ones (chess, an ELIZA-style roleplay, generative interactive fiction, a self-scoring introspection game).
- **Pure C++ RAG pipeline** — semantic chunker, binary `.db` index, deduplicating `RagSession`, with a folder picker for building indexes on the fly.
- **Web development mode** — a local HTTP server with live-reload that watches the sandbox for HTML changes. The model can push messages to the browser and trigger reloads, enabling a tight edit-preview loop. Enabled with `-p, --web-port <port>`.
- **Persistent settings** — configuration lives in `.hali/settings.json` under the sandbox, or point at a specific file with `-c`/`--config` (defaults to `.hali/hali.config.json`). Runtime data (input history, usage log, user-approved run list, plans, badges) is stored in the `.hali/` folder under the sandbox root.
- **Test suite** — unit tests under `tests/` (file operations, string/Unicode utilities, SHA-1, MCP message formatting, the graph renderer), runnable via CTest.

## Sandbox Security

Every tool that touches the filesystem is confined to a **sandbox root** — the directory Hali is launched in, or an explicit path argument. The security model is layered:

### Path validation

All file operations resolve their target path against the sandbox root using `std::filesystem::canonical()` (which resolves symlinks and `..` components) and verify the result is a prefix of the sandbox. This prevents directory traversal via `../../etc/passwd`, symlink escapes, and absolute-path injection.

Two access tiers are enforced:

| Tier | Function | Applies to |
|------|----------|------------|
| **Write** (strict) | `path_in_sandbox()` | `TOOL:WRITE`, `TOOL:PATCH`, `TOOL:APPEND`, `TOOL:MKDIR`, `TOOL:REPLACE` |
| **Read** (extended) | `path_allowed()` | `TOOL:LIST`, `TOOL:READ`, `TOOL:EXISTS`, `TOOL:SEARCH`, `TOOL:STAT`, `TOOL:HEX` |

Read access extends to a configurable list of **extra directories** (`extra_dirs` in config, or added at runtime with `/allow <path>`). Write access is always sandbox-only.

### Command execution

`TOOL:RUN` is gated by three mechanisms:

1. **Allowlist** — if `run_allowed` is set (comma-separated basenames), only those programs may execute. An empty allowlist permits any program inside the sandbox.
2. **Shell-metacharacter detection** — `has_shell_metachars()` rejects arguments containing `;`, `|`, `&`, `>`, `<`, `$`, backticks, or newlines that could enable command injection.
3. **Dangerous-pattern check** — `hasDangerousPatterns()` flags known-destructive commands (e.g. `rm -rf /`, `dd`, `mkfs`) even when they pass the allowlist.

User-approved programs are persisted to `.hali/user-approved.txt` under the sandbox with a 30-day TTL.

### Write protection

Before any write operation, Hali applies a series of validation gates:

- **Brace/paren balance** — for C-family and curly-brace languages, unbalanced content is flagged.
- **Size-shrink warning** — if new content is less than 50% of the original file size, a confirmation dialog appears.
- **Large-file warning** — files exceeding `val_write_size_warn` bytes (default 1000) trigger a warning.
- **Conflict-marker detection** — patch delimiters (`<<<<<<<`, `=======`, `>>>>>>>`) in the payload are flagged.
- **Timestamped backup** — if `--backup-path` is set, the original file is copied to a timestamped backup before the write.

These gates can be individually disabled via `val_write_validate` and `val_patch_validate` config keys for models that are known to produce well-formed output.

### Permission prompts

With `-n, --prompt-permission`, destructive tool calls (`TOOL:WRITE`, `TOOL:REPLACE`) require explicit user confirmation via a TUI dialog before executing. `TOOL:PERMISSION` provides a general-purpose confirmation mechanism for any action the model deems risky.

### Read caps

- `TOOL:READ` is capped at **32 KB** per call; larger files require `--lines=<start>,<end>` to read specific sections.
- `TOOL:CURL` responses are capped at **32 KB**.
- `TOOL:HEX` output is capped at **256 lines**.

### Git subcommand whitelist

`TOOL:GIT` only permits a fixed set of subcommands: `status`, `log`, `diff`, `add`, `commit`, `branch`, `show`. Arbitrary git commands (e.g. `push`, `reset --hard`, `config`) are rejected.

### Log hygiene

The `log_erase` config option (or `--log-erase`) truncates the log file at startup, preventing unbounded log growth across sessions.

## Slash commands

Inside the TUI, type a leading `/` to run a built-in command. Tab-completion is available for command names.

| Command              | Description                                                                             |
|----------------------|-----------------------------------------------------------------------------------------|
| `/model [path]`      | Load or hot-reload a GGUF model (opens a file picker if no path given)                  |
| `/embed [path]`      | Load an embedding model for RAG (file picker if no path)                                |
| `/rag [path]`        | Index a file or directory for RAG, or load an existing `.bin` index (picker if no path) |
| `/memory`            | Show KV cache, VRAM, and layer offload stats                                            |
| `/clear`             | Reset the conversation (rebuilds the system prompt)                                     |
| `/theme`             | Cycle through the Dark / Light / Navy themes (F12 also works)                           |
| `/audio [test\|volume <v>]` | Enable/disable audio, play a test tone, or set volume                                |
| `/save [file]`       | Save the current chat transcript to a file                                              |
| `/session [file]`    | Save a structured session snapshot (task, pending actions, errors, last output)          |
| `/settings`          | Display the current runtime settings                                                    |
| `/set <key> <value>` | Change a setting live (e.g. `/set temperature 0.7`)                                     |
| `/allow [path]`      | Grant read access to a directory outside the sandbox                                    |
| `/allow-list`        | List all directories with read access                                                   |
| `/allow-remove [path]` | Remove read access to a directory                                                     |
| `/revoke <name>`     | Remove an approved `TOOL:RUN` program from the allowlist                                |
| `/audit [path]`      | Scan the codebase for issues that are hard for an LLM to handle                         |
| `/lint`              | List source files and run clang-tidy lint checks                                        |
| `/skill [name] [kickoff]` | Load a skill file into the conversation (optionally with a kickoff prompt)          |
| `/help`              | Show this command list                                                                  |
| `exit` / `quit`      | Leave Hali                                                                              |

Settable keys via `/set`: `temperature`, `top_p`, `top_k`, `min_p`, `penalty_repeat`, `penalty_last_n`, `rag_top_k`, `n_gpu_layers`, `offload_kqv`, `run_allowed` (comma-separated list of allowed program basenames, or `none` to clear), `mtp_enabled`, `mtp_n_max`, `mtp_n_min`, `mtp_p_min`, `mtp_adaptive`, `mtp_pos_threshold`, `rope_freq_scale`, `rope_scaling_type`, `audio_enabled`, `audio_volume`, `audio_tool_tick`, `audio_user_prompt`, `val_write_validate`, `val_patch_validate`, `val_write_size_warn`, `log_erase`, `disable_run`, `disable_save_on_exit`.

## Building

Hali vendors llama.cpp as a submodule and links everything statically.

### Quick start

```
git clone --recurse-submodules https://github.com/chrisws/hali.git
cd hali
./setup.sh              # submodules, deps, build, smoke test — one shot
./build/bin/hali
```

`setup.sh` is a one-shot bootstrap: it checks for required dependencies (cmake, g++, make, notcurses), initialises submodules, builds the project, runs a `--help` smoke test, detects GPU VRAM (via a compiled CUDA probe or `nvidia-smi`), downloads an appropriate Qwen3 GGUF model from HuggingFace into `models/`, and generates a ready-to-use `.hali/hali.config.json` tuned to your hardware. Pass `--test` to also build and run the unit test suite.

### Manual build

```
cmake -B build -DLLAMA_BACKEND=AUTO
cmake --build build -j
./build/bin/hali
```

### Day-to-day maintenance

```
./maintain.sh           # build + test (default)
./maintain.sh build     # build only
./maintain.sh test      # run tests only
./maintain.sh clean     # clean build artifacts
```

### Running tests

```
cmake -B build-tests -S tests
cmake --build build-tests -j
ctest --test-dir build-tests
```

### Backend selection

| `-DLLAMA_BACKEND=` | Behavior |
| --- | --- |
| `AUTO` (default) | Probes for `nvcc`; falls back to CPU + OpenMP if CUDA isn't found |
| `CPU` | Forces CPU with native SIMD optimizations |
| `GPU` | Non-CUDA GPU path, CPU/OpenMP fallback |
| `CUDA` | Forces CUDA; fails the configure step if `nvcc` isn't found |

### Dependencies

| Library | Required | Notes |
| --- | --- | --- |
| notcurses | Yes | `apt install libnotcurses-dev`, or `-DNOTCURSES_DIR=<prefix>` |
| libcurl | No | Enables `TOOL:CURL`; `apt install libcurl4-openssl-dev`, or `-DCURL_DIR=<prefix>` |
| CUDA toolkit | No | Only for `-DLLAMA_BACKEND=CUDA`/`AUTO` with an NVIDIA GPU: `apt install nvidia-open cuda-toolkit` |
| yyjson | Vendored | JSON parsing for MCP and tool payloads; bundled under `yyjson/`, no system package needed |

## Usage

```
hali [sandbox-dir] [options]

  -m, --model <path>        path to a GGUF model
  -e, --embed <path>        path to an embedding model (for RAG)
  -g, --gpu-layers <n>      number of layers to offload to GPU
  -s, --skill <name>        load a skill file into the system prompt (repeatable)
  --mcp                     enable the MCP client
  --mcp-filter <name>       only expose this MCP tool (repeatable)
  --mcp-test                print the resolved MCP system context and exit
  -c, --config <path>       load settings from a specific JSON file
  -l, --log <path>          write logs to a file
  -t, --think               disable model "thinking" output
  -n, --prompt-permission   require explicit confirmation before destructive tool calls
  -p, --web-port <port>     enable web development mode with live-reload on the given port
  -b, --backup-path <path>  create file backups prior to invoking TOOL:WRITE
  -h, --help                show this help
```

A positional argument sets the sandbox root — the directory all file tools (`TOOL:LIST`/`TOOL:READ`/`TOOL:WRITE`/etc.) are confined to. It defaults to the current directory.

## Project layout

```
hali/
├── CMakeLists.txt
├── mcp.json                # MCP server connection config
├── .hali/                  # runtime data (config, history, plans, badges)
│   └── hali.config.json    # example runtime settings
├── setup.sh                # one-shot setup: submodules, deps, build, smoke test
├── maintain.sh             # day-to-day build / test / clean runner
├── apps/                   # standalone HTML games + AI stub servers
│   ├── chess.html          # chess (browser)
│   ├── checkers.html       # checkers (browser)
│   ├── battleships.html    # battleships (browser)
│   ├── space.html          # space shooter (browser)
│   └── ai-stub/            # Python WebSocket AI opponents (stdlib only)
│       ├── chess.py
│       ├── battleship.py
│       └── space.py
├── scripts/                # utility scripts
│   └── mtp_log_summary.py  # MTP performance log summariser
├── tools/                  # build-time utilities
│   └── gpu_probe.c         # CUDA VRAM probe (compiled by setup.sh)
├── skills/                 # markdown skill files, loaded via --skill
├── src/
│   ├── main.cpp            # entry point, CLI parsing, slash commands
│   ├── agent.cpp/.h        # agent loop, system prompt assembly, think parsing
│   ├── agent_dispatch.cpp  # tool dispatch table (all TOOL:* handlers)
│   ├── agent_utils.cpp/.h  # path helpers, tool arg parsing, SEARCH/REPLACE/TABLE, ThinkParser
│   ├── tui.cpp/.h          # notcurses UI, input, rendering, themes, search, mouse
│   ├── tui_context.h       # TUI context interface (events, rendering)
│   ├── config.cpp/.h       # settings, persistence, help text, prompt assembly
│   ├── llama_sb.cpp/.h     # llama.cpp wrapper
│   ├── llama_sb_mtp.cpp    # MTP speculative decoding
│   ├── llama_sb_suffix.cpp/.h # n-gram SuffixDrafter
│   ├── llama_sb_rag.cpp/.h # RAG session, chunker, indexer
│   ├── mcp_client.cpp/.h   # MCP protocol client
│   ├── mcp_format.cpp/.h   # MCP message formatting
│   ├── graph.cpp/.h        # TOOL:GRAPH ASCII chart/tree renderer
│   ├── curl.cpp/.h         # TOOL:CURL via libcurl
│   ├── file.cpp/.h         # sandboxed file tool implementations, path validation
│   ├── build.cpp/.h        # TOOL:BUILD (CMake/Gradle/Maven/Python auto-detect)
│   ├── json.cpp/.h         # yyjson wrapper
│   ├── string_utils.cpp/.h # Unicode-aware string helpers
│   ├── utf8.h              # UTF-8 utilities
│   ├── sha1.cpp/.h         # SHA-1 hashing (RAG dedup)
│   ├── webview.cpp/.h      # web development mode (HTTP server, live-reload)
│   ├── ui_text.cpp/.h      # in-app help, settings display, welcome banner
│   ├── input.cpp, input_event.h, input_history.h  # keyboard input handling
│   ├── logging.cpp/.h      # log file handling
│   ├── usage_log.cpp/.h    # per-turn metrics tracking, JSON export
│   ├── gpu_info.cpp/.h     # VRAM detection, KV restart threshold
│   ├── run_allowed.h       # persistent user-approved TOOL:RUN allowlist
│   ├── patch.h             # patch delimiters (PATCH_BEGIN/BOUNDARY/END)
│   ├── line_list.h         # line-list file I/O utility
│   ├── icon.h              # TUI icon constants
│   ├── audio.cpp/.h        # audio subsystem (init, tones, chords, scripts)
│   ├── audio_impl.cpp      # audio backend implementation
│   ├── planner.cpp/.h      # TOOL:PLAN structured goal/step tracking
│   ├── audit.cpp/.h        # /audit codebase scanner, false-positive tracking
│   ├── slash_commands.cpp/.h # slash command registry and completions
│   ├── shell_util.cpp/.h   # shell command execution, metacharacter detection
│   ├── sysinfo.cpp/.h      # TOOL:STAT, TOOL:SYSINFO, TOOL:HEX
│   └── git_tool.cpp/.h     # TOOL:GIT whitelisted subcommands
├── tests/                  # unit tests, CTest-driven
├── yyjson/                 # vendored JSON library
├── llama.cpp/              # submodule
└── forks/                  # forked upstream projects (e.g. Strata)
```

## Apps

The [`apps/`](apps/) directory contains a collection of standalone, dependency-free HTML games that run entirely in the browser. Each game is a 
single self-contained `.html` file (no build step, no npm, no CDN) designed to be served via Hali's web development mode (`-p, --web-port <port>`) for a tight edit-preview loop.

| Game        | File               | Description                                                                 |
|-------------|--------------------|-----------------------------------------------------------------------------|
| Chess       | `chess.html`       | Full chess with drag-and-drop, move validation, and a WebSocket AI opponent |
| Checkers    | `checkers.html`    | American checkers with jump captures and king promotion                     |
| Battleships | `battleships.html` | Classic naval combat with random or AI-placed fleets                        |
| Space       | `space.html`       | Arcade space shooter with waves, power-ups, and high scores                 |

## License

GPL2 — see `LICENSE`.
