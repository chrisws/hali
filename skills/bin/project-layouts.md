# Skill: Project Layouts for Unit Tests

This skill documents the expected folder structures for C++, Java, Kotlin,
and JavaScript projects. Use it when refactoring code, adding new modules,
or migrating projects to ensure unit tests land in the right place and the
build system picks them up correctly.

---

## C++ (CMake)

### Expected Layout

```
project/
├── CMakeLists.txt              # Top-level build (main executable)
├── src/
│   ├── main.cpp                # Entry point
│   ├── core/                   # Core logic modules
│   │   ├── agent.cpp
│   │   ├── agent.h
│   │   └── ...
│   ├── model/                  # Model / inference layer
│   │   ├── llama_iter.cpp
│   │   ├── llama_iter.h
│   │   └── ...
│   ├── ui/                     # UI / TUI layer
│   │   ├── tui.cpp
│   │   ├── tui.h
│   │   └── ...
│   ├── tools/                  # Tool implementations
│   │   ├── file.cpp
│   │   ├── file.h
│   │   └── ...
│   └── util/                   # Shared utilities
│       ├── string_utils.cpp
│       ├── string_utils.h
│       └── ...
├── tests/
│   ├── CMakeLists.txt          # Standalone test build (independent of main)
│   └── src/
│       ├── main.cpp            # Test runner entry point
│       ├── <module>_test.cpp   # One file per module under test
│       └── <module>_stub.cpp   # Stubs replacing heavy/external deps
├── cmake/                      # CMake helper modules
├── libs/                       # Vendored third-party headers
└── yyjson/                     # Vendored C library (submodule)
```

### Key Conventions

| Rule | Detail |
|------|--------|
| Headers co-located | `.h` lives next to its `.cpp` in the same `src/<module>/` dir |
| One test file per module | `src/core/agent.cpp` → `tests/src/agent_test.cpp` |
| Stubs for externals | Heavy deps (TUI, GPU, network) get a `*_stub.cpp` in `tests/src/` |
| Standalone test build | `tests/CMakeLists.txt` is self-contained; can `cd tests && cmake && make` |
| Source discovery | `file(GLOB ...)` per module directory in both CMakeLists files |
| Exclusions | Test CMakeLists explicitly `list(REMOVE_ITEM ...)` files replaced by stubs |
| Test protocol | Custom HTP (Hali Test Protocol): `PASS <name>` / `FAIL <name> <msg>` |
| C++ standard | C++20 (`set(CMAKE_CXX_STANDARD 20)`) |
| Include paths | Each `src/<module>/` dir added to `target_include_directories` |

### Refactoring Checklist (C++)

1. **New module** → create `src/<module>/` with `.cpp` + `.h`, add to GLOB in both
   `CMakeLists.txt` files (main + tests).
2. **New test** → add `tests/src/<module>_test.cpp`, forward-declare the test
   function in `tests/src/main.cpp`, add `run_htp_test(...)` call.
3. **External dep in test** → write a `tests/src/<dep>_stub.cpp` that provides
   the same symbols, then `list(REMOVE_ITEM ...)` the real file in
   `tests/CMakeLists.txt`.
4. **Move a function** → update `#include` paths; headers are found via
   per-module include dirs, so `#include "module/thing.h"` from any module.
5. **Split a large file** → keep the module directory; add new `.cpp`/`.h`
   pair; GLOB picks it up automatically.

---

## Java (Maven)

### Expected Layout

```
project/
├── pom.xml
├── src/
│   ├── main/
│   │   ├── java/
│   │   │   └── com/
│   │   │       └── example/
│   │   │           ├── App.java
│   │   │           ├── core/
│   │   │           │   ├── Service.java
│   │   │           │   └── Repository.java
│   │   │           ├── model/
│   │   │           │   └── Entity.java
│   │   │           └── util/
│   │   │               └── StringUtils.java
│   │   └── resources/
│   │       └── static/          # Frontend JS/CSS served by the app
│   │           └── modules/
│   │               ├── url-utils.js
│   │               └── nav-history.js
│   └── test/
│       ├── java/
│       │   └── com/
│       │       └── example/
│       │           ├── core/
│       │           │   ├── ServiceTest.java
│       │           │   └── RepositoryTest.java
│       │           └── util/
│       │               └── StringUtilsTest.java
│       └── js/                  # JavaScript unit tests (Node)
│           ├── run-tests.js     # Auto-discovery runner
│           ├── dom-shim.js      # Minimal DOM mock for browser modules
│           ├── url-utils.test.js
│           └── nav-history.test.js
└── (test resources: src/test/resources)
```

### Key Conventions

| Rule | Detail |
|------|--------|
| Package mirrors directory | `com.example.core` → `src/main/java/com/example/core/` |
| Test package mirrors source | `com.example.core.Service` → `com.example.core.ServiceTest` |
| One test class per class | `Service.java` → `ServiceTest.java` (same package) |
| Test framework | JUnit 5 (`@Test`, `@BeforeEach`, `@AfterEach`) |
| JS tests | `src/test/js/*.test.js` using Node's built-in `node:test` |
| JS test integration | `exec-maven-plugin` in `pom.xml` runs `node run-tests.js` at `test` phase |
| Build tool | Maven (`pom.xml`) — `mvn test` runs **both** Java and JS tests |
| No co-located headers | Java has no separate header files |
| Resources | `src/main/resources/` for config/static, `src/test/resources/` for fixtures |

### JS Test Integration via `exec-maven-plugin`

When a Maven project also ships frontend JavaScript (e.g. static modules
served by a Javalin/Spring server), add JS unit tests under `src/test/js/`
and wire them into the Maven `test` phase with `exec-maven-plugin`:

```xml
<plugin>
  <groupId>org.codehaus.mojo</groupId>
  <artifactId>exec-maven-plugin</artifactId>
  <version>3.1.0</version>
  <executions>
    <execution>
      <id>js-tests</id>
      <phase>test</phase>
      <goals><goal>exec</goal></goals>
      <configuration>
        <executable>node</executable>
        <arguments>
          <argument>${project.basedir}/src/test/js/run-tests.js</argument>
        </arguments>
      </configuration>
    </execution>
  </executions>
</plugin>
```

The `run-tests.js` entry point auto-discovers all `*.test.js` files in its
own directory and invokes `node --test` on them:

```js
import { execSync } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const dir = dirname(fileURLToPath(import.meta.url));
const files = readdirSync(dir)
  .filter(f => f.endsWith('.test.js'))
  .map(f => join(dir, f));

if (files.length === 0) {
  console.log('No test files found.');
  process.exit(0);
}

execSync(`node --test ${files.join(' ')}`, { stdio: ['ignore', 'pipe', 'inherit'] });
```

This means `mvn test` runs **both** JUnit tests (via surefire) **and**
Node JS tests (via exec plugin) in a single command. No separate npm/test
runner needed — Node's built-in test runner is sufficient.

### Refactoring Checklist (Java)

1. **New package** → create dir under `src/main/java/com/example/<pkg>/`,
   add corresponding `src/test/java/com/example/<pkg>/` for tests.
2. **Move a class** → move `.java` file, update `package` declaration,
   update all `import` statements in dependent files (main + test).
3. **New test** → create `<ClassName>Test.java` in the **same package** under
   `src/test/java/`.
4. **Split a class** → new `.java` file in same package; add matching test.
5. **Extract interface** → interface in `src/main/java/.../core/`,
   implementation alongside; test against the interface.
6. **New JS module** → add `src/main/resources/static/modules/<name>.js`,
   create `src/test/js/<name>.test.js`; `run-tests.js` picks it up
   automatically (no registration needed).
7. **JS module uses DOM** → import `dom-shim.js` at the top of the test
   file before importing the module under test.

---

## Kotlin (Gradle)

### Expected Layout

```
project/
├── build.gradle.kts
├── settings.gradle.kts
├── src/
│   ├── main/
│   │   └── kotlin/
│   │       └── com/
│   │           └── example/
│   │               ├── App.kt
│   │               ├── core/
│   │               │   ├── Service.kt
│   │               │   └── Repository.kt
│   │               ├── model/
│   │               │   └── Entity.kt
│   │               └── util/
│   │                   └── StringUtils.kt
│   └── test/
│       └── kotlin/
│           └── com/
│               └── example/
│                   ├── core/
│                   │   ├── ServiceTest.kt
│                   │   └── RepositoryTest.kt
│                   └── util/
│                       └── StringUtilsTest.kt
└── (resources: src/main/resources, src/test/resources)
```

### Key Conventions

| Rule | Detail |
|------|--------|
| Package mirrors directory | Same as Java — `com.example.core` → `.../com/example/core/` |
| Test package mirrors source | `Service.kt` → `ServiceTest.kt` in same package |
| One test file per class | Can contain multiple `@Test` functions per file |
| Test framework | JUnit 5 or Kotest (Kotest preferred for Kotlin idioms) |
| Build tool | Gradle (`build.gradle.kts`) — `./gradlew test` runs all tests |
| No separate headers | Kotlin is single-file per class (or multiple classes per file) |
| `src/main/kotlin/` not `src/main/java/` | Gradle Kotlin plugin uses `kotlin` source set dir |

### Refactoring Checklist (Kotlin)

1. **New package** → create dir under `src/main/kotlin/com/example/<pkg>/`,
   mirror in `src/test/kotlin/com/example/<pkg>/`.
2. **Move a class** → move `.kt` file, update `package` line, update imports.
3. **New test** → `<ClassName>Test.kt` in same package under `src/test/kotlin/`.
4. **Split a file** → Kotlin allows multiple top-level classes per file,
   but prefer one class per file for clarity; add matching test.
5. **Extract interface** → interface + impl in same package; test against
   the interface using mocks (MockK).

---

## JavaScript (Node)

### Expected Layout

```
project/
├── src/
│   └── main/
│       └── resources/
│           └── static/
│               └── modules/       # Production JS modules
│                   ├── url-utils.js
│                   ├── nav-history.js
│                   └── keybindings.js
└── src/
    └── test/
        └── js/
            ├── run-tests.js       # Auto-discovery runner (entry point)
            ├── dom-shim.js        # Minimal DOM mock for browser modules
            ├── url-utils.test.js
            ├── nav-history.test.js
            └── keybindings.test.js
```

> In a standalone Node project (no Maven/Gradle wrapper), the layout is
> simply `src/modules/` for source and `test/` for tests, with
> `package.json` scripts: `"test": "node --test test/"`.

### Key Conventions

| Rule | Detail |
|------|--------|
| Test naming | `<module>.test.js` — mirrors the source filename |
| Test framework | Node built-in `node:test` + `node:assert` (zero external deps) |
| Auto-discovery | `run-tests.js` globs `*.test.js` in its own dir; no registration |
| DOM mocking | `dom-shim.js` provides `document`, `window`, `createElement`, etc. |
| Import style | ES modules (`import ... from '...'`); tests import production code via relative path |
| Integration | `exec-maven-plugin` (Maven) or `package.json` script (standalone) |
| No npm test runner | No Jest, no Mocha — `node --test` is sufficient |

### DOM Shim Pattern

When testing browser modules (DOM manipulation, event handling) under Node,
import `dom-shim.js` **before** the module under test:

```js
import './dom-shim.js';  // Must come first — sets up globalThis.document
import { buildSearchUrl } from '../../main/resources/static/modules/url-utils.js';

import { test } from 'node:test';
import assert from 'node:assert';

test('buildSearchUrl: basic query', () => {
  assert.strictEqual(buildSearchUrl('foo'), '/api/search?q=foo');
});
```

The shim provides:
- `globalThis.document` — `createElement`, `getElementById`, `querySelector`
- `globalThis.window` — `location`, `prompt`, `confirm`, `alert`
- Mock elements with `classList`, `setAttribute`, `appendChild`, `addEventListener`
- Pre-registered element IDs for common app panels

### Refactoring Checklist (JavaScript)

1. **New module** → add `src/main/resources/static/modules/<name>.js`,
   create `src/test/js/<name>.test.js`; `run-tests.js` picks it up
   automatically.
2. **Module uses DOM** → import `dom-shim.js` at top of test file.
3. **Move a module** → update the relative import path in the test file.
4. **Split a module** → new `.js` file + new `.test.js`; both auto-discovered.
5. **Add a new DOM element ID** → register it in `dom-shim.js`'s
   `_elements` map so `getElementById` returns a mock.

---

## Cross-Language Comparison

| Aspect | C++ (CMake) | Java (Maven) | Kotlin (Gradle) | JS (Node) |
|--------|-------------|--------------|-----------------|-----------|
| Source root | `src/<module>/` | `src/main/java/<pkg>/` | `src/main/kotlin/<pkg>/` | `src/main/resources/static/modules/` |
| Test root | `tests/src/` | `src/test/java/<pkg>/` | `src/test/kotlin/<pkg>/` | `src/test/js/` |
| Headers | Co-located `.h` | None | None | None |
| Test naming | `<module>_test.cpp` | `<Class>Test.java` | `<Class>Test.kt` | `<module>.test.js` |
| Build cmd | `cmake && make` | `mvn test` | `./gradlew test` | `node --test` |
| Test discovery | Manual (GLOB + main.cpp) | Automatic (JUnit) | Automatic (JUnit/Kotest) | Automatic (`run-tests.js` glob) |
| Stubs/mocks | Hand-written `*_stub.cpp` | Mockito | MockK | `dom-shim.js` |
| Standalone test build | Yes (`tests/CMakeLists.txt`) | No (same `pom.xml`) | No (same `build.gradle.kts`) | Yes (`node --test`) |
| External test deps | None (HTP) | JUnit 5 | JUnit 5 / Kotest | None (node:test) |

---

## General Refactoring Principles

1. **Tests follow code** — if you move a source file, move (or create) its
   test in the corresponding test directory.
2. **One module = one directory** — don't scatter files; group by
   responsibility (core, model, ui, tools, util).
3. **Stubs over real deps in tests** — keep test builds fast and isolated;
   replace network, GPU, TUI, and other heavy deps with stubs.
4. **Build system must discover new files** — CMake GLOB is automatic;
   Maven/Gradle pick up files by convention; `run-tests.js` globs
   `*.test.js` (no registration needed for any language).
5. **Keep test builds independent** — especially in C++, the test
   `CMakeLists.txt` should compile without the main executable's
   full dependency chain where possible.
6. **Naming is contract** — `<module>_test.cpp`, `<Class>Test.java`,
   `<Class>Test.kt`, `<module>.test.js` — deviating breaks tooling
   and human expectations.
7. **One build command runs all tests** — `mvn test` should run both
   Java (surefire) and JS (exec-maven-plugin → node) tests. `cmake && make`
   should run all C++ tests. `./gradlew test` should run all Kotlin tests.
   The user should never need to remember a second test command.
8. **Prefer built-in test runners** — Node's `node:test`, C++'s HTP,
   JUnit 5 — avoid pulling in Jest, Mocha, or GTest when the built-in
   option is sufficient. Fewer dependencies = fewer version conflicts.