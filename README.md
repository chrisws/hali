# H · A · L · I | Haliastur | Hardware-Aware Local Inference

**A standalone, agentic LLM shell for your terminal.**

Hali is a local-first agentic coding/chat shell built on [llama.cpp](https://github.com/ggml-org/llama.cpp), rendered with [notcurses](https://github.com/dankamongmen/notcurses). No server, no browser tab, no cloud dependency — just a fast terminal UI driving a local model with tool use, RAG, MCP, Web development server and careful context management.

<img width="831" height="771" alt="Screenshot From 2026-09-26 07-42-59" src="https://github.com/user-attachments/assets/105b2376-d5c0-48b9-b0d6-6922a758e6cc" />

## Why

Most agentic shells assume a hosted API and treat context as free. Hali assumes neither: it's built for consumer GPUs (developed against a 16GB RTX 5060 Ti) where the KV cache is a scarce resource and every tool call has to earn its place in the context window.

## Features

- **notcurses TUI** — plane-based rendering, modal popups, persistent input history (Up/Down navigation), live `/set` commands for generation parameters, in-chat search, and Kitty keyboard protocol support for reliable input in modern terminals.
- **Theme system** — three built-in themes (Dark, Light, Navy) switchable at runtime with `/theme` or F12.
- **Fragmentation-aware KV cache management** — `full_flush_except_system()` as a graceful recovery path when sequence removal fragments the cache instead of compacting it; a `KVCachePreset` enum (`F16` / `Balanced` / `Compact`) coupled to flash-attention settings.
- **Dynamic tool-result budgeting** — `max_tool_result_size()` targets ~75% of remaining context so a single large tool result can't blow the budget.
- **Unambiguous tool-call protocol** — an explicit `HALI_END_TOOL` terminator so tool boundaries never get confused with model chatter.
- **Multi-Token Prediction (MTP)** — optional speculative decoding via MTP-capable GGUF models. Configurable with `mtp_enabled`, `mtp_n_max`, `mtp_n_min`, and `mtp_p_min` settings to control how many draft tokens are proposed and the acceptance threshold.
- **Suffix drafting** — a lightweight n-gram `SuffixDrafter` that matches recent tokens against earlier context and drafts continuations, helping with repetitive/structured text when MTP acceptance is low.
- **GPU introspection** — `gpu_info` detects VRAM via the ggml backend API and scales the KV-cache restart threshold (8 GB → 75 %, 16 GB → 85 %, 32 GB → 90 %, cap 92 %).
- **Usage logging** — per-turn metrics (LLM latency, completion tokens, tool calls/errors, KV usage, files read/written) tracked in a `UsageLog` with JSON export and KV-trend detection.
- **A full sandboxed tool suite** — every file operation is scoped to a sandbox root (the directory Hali is launched in, or an explicit path argument):

  | Tool                                                    | Purpose                                                                                                                                                                                                                                                |
  |---------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
  | `TOOL:LIST`, `TOOL:READ`, `TOOL:EXISTS`                 | Inspect files inside the sandbox                                                                                                                                                                                                                       |
  | `TOOL:SEARCH`                                           | Search file contents by pattern with rich flags: `--recursive`, `--line-numbers`, `--count`, `--line-count`, `--files-only`, `--name` (match filenames), `--include=<glob>`, `--context=<n>` (surrounding lines), `--lines=<start>,<end>` (line range) |
  | `TOOL:WRITE`, `TOOL:APPEND`, `TOOL:PATCH`, `TOOL:MKDIR` | Modify files inside the sandbox                                                                                                                                                                                                                        |
  | `TOOL:REPLACE`                                          | Replace text in a file with `--all` and `--count` flags                                                                                                                                                                                                |
  | `TOOL:RUN`                                              | Run a program inside the sandbox, gated by an optional `run_allowed` allowlist                                                                                                                                                                         |
  | `TOOL:BUILD`                                            | Auto-detect and run project build systems (CMake, Gradle, Maven, Python) with modes: `setup`, `build`, `test`, `clean`                                                                                                                                 |
  | `TOOL:CURL`                                             | HTTP GET via libcurl, with HTML-to-text stripping and a response size cap                                                                                                                                                                              |
  | `TOOL:RAG`                                              | Query the local RAG index for extra context                                                                                                                                                                                                            |
  | `TOOL:ASK`                                              | Ask the user directly, for interactive/agentic flows                                                                                                                                                                                                   |
  | `TOOL:GRAPH`                                            | Render bar charts and tree diagrams as ASCII art from a small JSON schema                                                                                                                                                                              |
  | `TOOL:TABLE`                                            | Format pipe-delimited rows into an aligned text table                                                                                                                                                                                                  |
  | `TOOL:MCP`                                              | Invoke a tool on a connected MCP server                                                                                                                                                                                                                |
  | `TOOL:PERMISSION`                                       | Explicit user confirmation before destructive or irreversible actions                                                                                                                                                                                  |
  | `TOOL:RESTART`                                          | Hand off to a fresh session, writing current task context to `SESSION.md` first                                                                                                                                                                        |
  | `TOOL:DATE`, `TOOL:TIME`, `TOOL:RND`, `TOOL:INTROSPECT` | Small utility/introspection tools                                                                                                                                                                                                                      |

- **Persistent run-allowlist** — user-approved `TOOL:RUN` program names are saved to `~/.config/hali/user-approved.txt` with timestamps and a 30-day TTL, so approvals survive across sessions without re-prompting.
- **MCP client** — connect to external Model Context Protocol servers (e.g. JetBrains IDE built-in MCP servers) with `--mcp`, filter which tools get exposed with `--mcp-filter`, and dry-run the resulting system context with `--mcp-test`. Server connection details live in `mcp.json`.
- **Skills system** — load one or more markdown skill files into the static system-prompt prefix at session start with `--skill <name>` (no per-turn routing, so it doesn't disrupt the KV cache). `hali.md`, `persona.md`, and `AGENTS.md` are auto-discovered from the current directory if present. The [`skills/`](skills/) folder ships a starter set covering debugging, TDD, code review, CMake/build troubleshooting, memory-safety review, dependency-free frontend work, SmallBASIC's raylib plugin, free JSON API sourcing, a local-info feed pattern, planning, spiking, code simplification, house style, skill import, and a few just-for-fun ones (chess, an ELIZA-style roleplay, generative interactive fiction, a self-scoring introspection game).
- **Pure C++ RAG pipeline** — semantic chunker, binary `.db` index, deduplicating `RagSession`, with a folder picker for building indexes on the fly.
- **Web development mode** — a local HTTP server with live-reload that watches the sandbox for HTML changes. The model can push messages to the browser and trigger reloads, enabling a tight edit-preview loop. Enabled with `-p, --web-port <port>`.
- **Persistent settings** — configuration lives in `~/.config/hali.settings.json`, or point at a specific file with `-c`/`--config` (defaults to `hali.config.json`).
- **Test suite** — unit tests under `tests/` (file operations, string/Unicode utilities, SHA-1, MCP message formatting, the graph renderer), runnable via CTest.

## Slash commands

Inside the TUI, type a leading `/` to run a built-in command:

| Command              | Description                                                                             |
|----------------------|-----------------------------------------------------------------------------------------|
| `/model [path]`      | Load or hot-reload a GGUF model (opens a file picker if no path given)                  |
| `/embed [path]`      | Load an embedding model for RAG (file picker if no path)                                |
| `/rag [path]`        | Index a file or directory for RAG, or load an existing `.bin` index (picker if no path) |
| `/memory`            | Show KV cache, VRAM, and layer offload stats                                            |
| `/clear`             | Reset the conversation (rebuilds the system prompt)                                     |
| `/theme`             | Cycle through the Dark / Light / Navy themes (F12 also works)                           |
| `/save [file]`       | Save the current chat transcript to a file                                              |
| `/settings`          | Display the current runtime settings                                                    |
| `/set <key> <value>` | Change a setting live (e.g. `/set temperature 0.7`)                                     |
| `/help`              | Show this command list                                                                  |
| `exit` / `quit`      | Leave Hali                                                                              |

Settable keys via `/set`: `temperature`, `top_p`, `top_k`, `min_p`, `penalty_repeat`, `penalty_last_n`, `rag_top_k`, `n_gpu_layers`, `offload_kqv`, `run_allowed` (comma-separated list of allowed program basenames, or `none` to clear), `mtp_enabled`, `mtp_n_max`, `mtp_n_min`, `mtp_p_min`.

## Building

Hali vendors llama.cpp as a submodule and links everything statically.

### Quick start

```
git clone --recurse-submodules https://github.com/chrisws/hali.git
cd hali
./setup.sh              # submodules, deps, build, smoke test — one shot
./build/bin/hali
```

`setup.sh` checks for required dependencies (cmake, g++, make, notcurses), initialises submodules, builds the project, and runs a `--help` smoke test. Pass `--test` to also build and run the unit test suite.

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
├── hali.config.json        # example runtime settings
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
├── skills/                 # markdown skill files, loaded via --skill
├── src/
│   ├── main.cpp            # entry point, CLI parsing, slash commands
│   ├── agent.cpp/.h        # tool dispatch, system prompt assembly
│   ├── agent_utils.cpp/.h  # path helpers, tool arg parsing, SEARCH/REPLACE/TABLE
│   ├── tui.cpp/.h          # notcurses UI, input, rendering, themes, search
│   ├── tui_context.h       # TUI context interface (events, rendering)
│   ├── config.cpp/.h       # settings, persistence, help text
│   ├── llama_sb.cpp/.h     # llama.cpp wrapper
│   ├── llama_sb_mtp.cpp    # MTP speculative decoding
│   ├── llama_sb_suffix.cpp/.h # n-gram SuffixDrafter
│   ├── llama_sb_rag.cpp/.h # RAG session, chunker, indexer
│   ├── mcp_client.cpp/.h   # MCP protocol client
│   ├── mcp_format.cpp/.h   # MCP message formatting
│   ├── graph.cpp/.h        # TOOL:GRAPH ASCII chart/tree renderer
│   ├── curl.cpp/.h         # TOOL:CURL via libcurl
│   ├── file.cpp/.h         # sandboxed file tool implementations
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
│   └── icon.h              # TUI icon constants
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
