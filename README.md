# CForge Build System

CForge is a minimal, low-level "build system" that exports utilities needed for build scripts in C. This includes simple string operations, globs, path manipulation, parallel compilation, custom build profiles, and incremental builds. The main philosophy of CForge is not to make yet another completely new language for a build system, but to rather make a lightweight DSL on top of C that merely simplifies your life as a developer. In other words, your build system will be in C (a language you should understand well if you are using this system) and as such you do not have to re-learn anything. This comes with the convenience your build system being "blazing fast", minimal, and zero-dependency (well, except for POSIX as it stands currently).

## History

At the time of writing this library, I got extremely fed up with [Makefiles](https://www.gnu.org/software/make/manual/html_node/Introduction.html). This is mostly because of their poor [debuggability](https://en.wiktionary.org/wiki/debuggability) while writing yet another tool that I could not find on the WWW: [mdprev](https://github.com/Wrench56/mdprev), a GitHub-style Markdown previewer (for the browser), with hot-reload features and full LaTeX support.

This project was influenced by [the nob build system](https://github.com/tsoding/nob.h/). Most of the design changes I made are to make the use of nob-like systems easier with some C/Shellscript hackery.

## Documentation

### Dependencies

Nothing beyond a C11 toolchain.

### Supported Platforms

Anything with POSIX support. Currently, Windows is not supported. This might change in the future. Some platforms received special (subtle) optimizations, namely FreeBSD and Linux.

### Install

#### Manual

1. Drop `cforge.h` into your project root and make it executable:

```sh
chmod +x cforge.h
```

2. Create a build file named `cforge.c` next to it:

```c
#include "cforge.h"

CF_TARGET(build) {
    CF_RUN("cc -O2 -o app main.c");
}
```

3. Run the target:

```sh
./cforge.h build
```

#### Repo-Init

One might also use the [repo-init](https://github.com/Wrench56/repo-init) tool to automatically fetch `cforge.h`. Steps 2 and 3 remain the same.

### CLI

The CForge CLI is rather simple:

If no argument was provided, CForge automatically prints a usage text along with all (publicly) available targets with their help texts.
If one or more arguments are provided, each are interpreted as a target and ran in sequence.

### Compile-Time Options

CForge exposes some internal tuneables. I strived to make the defaults sensible choices, but sometimes it is worth tuning things a bit.

| Define | Effect |
| ------ | ------ |
| `CF_DISABLE_FILE_HASH` | Skip content hashing in the up-to-date cache. This means the caching mechanism is going to only rely on size, mtime, and the environment hash.
| `CF_DISABLE_ENV_AUTOMASK` | Do not unset interactive-session environment variables at startup. By default, per-session environment variables are unset to not disturb the up-to-date cache.

### API

#### Targets

- `CF_TARGET(name, ...attributes)`: declares a target (akin to a Makefile target). The `...` holds [attributes](https://github.com/Wrench56/cforge#attributes). By running `./cforge.h <target>`, the declared target will run.

##### Attributes

- `CF_DEPENDS(target)`: Run the provided `target` before this one. A parent target (that is, a target that depends on other targets) will verify if the dependency ran and if not, it will call them in sequence.
- `CF_WITH_CONFIG(config)`: Apply `config` to this target and its dependencies. **Any `CF_WITH_CONFIG` MUST come before the first `CF_DEPENDS`!**
- `CF_HELP_STRING(str)`: The text shown in the no-args usage listing.
- `CF_HIDDEN`: Omit this target from the usage listing.
- `CF_VERBOSE`: Print each command before running it (for this target only) akin to non-`@` behavior in Makefiles.

#### Configs

- `CF_CONFIG(name)`: declares a configuration. A target can use one configuration only. The config will be ran before the target and provide environment setups. A common example is having a config for `release` and `debug` profile. These would set different environment variables to influence the target's execution. A parent target (that is, a target that depends on other targets) will pass its required config down the chain.

Before each target runs, the environment is checkpointed. After it finishes, every change is rolled back. This ensures that config-driven environment mutations are confined to the target subtree that set them and never leak upwards or sideways (into other adjacent targets).

The usual building blocks found in the body of a configuration are the following:

- `CF_CONFIG_EXTENDS(config)`: extend an existing and defined configuration. Under the hood it just calls the specified configuration function. Useful when you have a common set of environment variables across build profiles. E.g. linked libraries do not change between `release` and `debug` profiles.
- `CF_SET_ENV(ident, value)`: set a given environment variable.
- `CF_APPEND_ENV(ident, value)`: append a string to the end of an environment variable. If the environment variable does not exist, create it with the given value.
- `CF_PREPEND_ENV(ident, value)`: prepend a string to the front of an environment variable. If the environment variable does not exist, create it with the given value.
- `CF_MASK_ENV(ident)`: equivalent to `CF_SET_ENV(ident, "")`.
- `CF_ENV(ident)`: returns the value of the environment variable specified by `ident`.

#### Command Execution

`CF_RUN(...)` executes a command synchronously and inline. `CF_RUNP(...)` enqueues it onto a bounded work queue drained by lazily created worker threads. Each worker calls `system()` and a non-zero return will abort the build.

A target is a synchronization barrier. Before a target is marked done, the executor waits for all in-flight and queued jobs to finish. This ensures that dependent targets can safely consume the outputs of a parallel dependency.

Functions that have a parallel execution specific version have a `P` postfix (e.g. `CF_RUNP` instead of `CF_RUN`).

#### Up-To-Date Caching (UTD Caching)

CForge keeps track of up-to-date files in a small cache database `.cforge.db`. A file is considered up to date when all of the following records match: the file's size, mtime (both seconds and nanoseconds), the environment hash, and content hash (if `CF_DISABLE_FILE_HASH` is not defined). Usually file hash checks are unnecessary and are not done by many build systems. That said, the hash implementation is one of the fastest one available and thus it doesn't cost much to hash the tracked files.

- `CF_FILE_MARK_UTD(path)`: marks file as up-to-date immediately. Use it along with `CF_RUN(...)`.
- `CF_FILE_MARK_UTDP(path)`: defers the mark until the target barrier. This means a check within the target marking a file up-to-date using this macro won't necessarily take effect until the end of the target's lifetime. Therefore ensure that a target using `CF_RUNP(...)` does not check recent UTD marks. Use it along with `CF_RUNP(...)`.
- `CF_FILE_UTD(path)`: checks if file is up-to-date.
- `CF_FILE_NOT_UTD(path)`: inverse of the above. Typical use pattern:

```c
if CF_FILE_NOT_UTD(src) {
    rebuilt(src);
}

```

#### File Operations

Some ubiquitous file operation helpers are exposed for cross-platform compatibility. Convenient features are automatically enabled, such as `-p` for `mkdir(1)` or `-r` for `cp(1)`.

- `CF_FILE_EXISTS(filepath)`: checks if a file exists.
- `CF_MKDIR(path)`: create a new directory. If parent directory/directories do not exist, create them too.
- `CF_MV(src, dst)`: move a file or a directory.
- `CF_CP(src, dst)`: copy a file or a directory.
- `CF_RM(src, dst)`: remove a file or a directory.
- `CF_WRITE(path, ...)`: write the provided formatted string into a file.
- `CF_APPEND(path, ...)`: append the provided formatted string to a file.
- `CF_READ(path)`: read the provided file. Returns a pointer to a string which is the full content of the file.

#### Utilities

- `CF_GLOB(expr)`: Returns a `cf_glob_t` struct of the given expression. The struct has two fields: `p` and `c`. `p` is the array itself containing the globs while `c` is the counted number of globs.
- `CF_GLOBS_EACH(expr, filename)`: functions as a for loop over all files matched by the POSIX glob `expr` and storing the path inside the variable whose name is passed into `filename`. Typical use:

```c
for CF_GLOBS_EACH("src/*", filepath) {
    printf("Compiling %s...", filepath);
    compile(filepath);
}
```

- `CF_MAP(source, ...)`: manipulate a path using `CF_MAP_EXT`, `CF_MAP_PARENT`, and `CF_MAP_DIRS`.
    - `CF_MAP_EXT(new_ext)`: changes the extension of a file to `new_ext`. The `.` is automatically appended.
    - `CF_MAP_PARENT(new_parent)`: sets the uppermost directory to `new_parent`.
    - `CF_MAP_DIRS(new_dirs)`: sets all of the directories to `new_dirs`.
- `CF_MAPA(sources, len, ...)`: same as `CF_MAP` except this macro operates on an array of paths. Typical use:

```c
cf_glob_t srcs = CF_GLOB("src/*.c");
char** outs = CF_MAPA(
    srcs.p,
    srcs.c,
    CF_MAP_EXT("o"),
    CF_MAP_PARENT("build")
);
```

- `CF_JOIN(arr, sep, len)`: joins the array `arr` together with separator `sep`. `len` argument holds the length of the array.
- `CF_JOIN_GLOB(glob, sep)`: same as above except you only pass a `cf_glob_t` struct to it. Equivalent to `CF_JOIN(glob.p, sep, glob.c)`.
- `CF_SPLIT(str, delim)`: splits a string into an array at each delimiter `delim`.
