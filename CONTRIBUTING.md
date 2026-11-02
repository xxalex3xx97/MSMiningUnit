# Contributing

`main` is protected. Never push to it directly.

1. Make a feature branch: `git switch -c <topic>`.
2. Commit and push the branch, then open a pull request into `main`.
3. Run `/code-review` on the pull request and fix what it finds.
4. Wait for the `ci` check to pass.
5. The owner approves and merges the pull request.

CI runs only what needs no game or RE-UE4SS: Lua syntax (`luac -p`), unit tests and Python tests. Build, install and release scripts run locally (build.ps1, scripts\).
