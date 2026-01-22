# CForge Build System

At the time of writing this library, I got extremely fed up with [Makefiles](https://www.gnu.org/software/make/manual/html_node/Introduction.html). This is mostly because of their poor [debuggability](https://en.wiktionary.org/wiki/debuggability) while writing yet another tool that I could not find on the WWW: [mdprev](https://github.com/Wrench56/mdprev), a GitHub-style Markdown previewer (for the browser), with hot-reload features and full LaTeX support.

This project was influenced by [the nob build system](https://github.com/tsoding/nob.h/). Most of the changes I made are to make the use of nob-like systems easier with some C/Shellscript hackery.
