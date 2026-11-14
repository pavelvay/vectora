# Project workflow (Vectora)

## Build and publish

- Do **not** build release artifacts, create tags, or upload anything to GitHub
  unless the user's message explicitly contains the word **«сборка»**
  (a build/publish request).
- Mentioning the rule itself, an issue, a feature, or a code change is **not**
  a build request.
- When a build **is** requested: run the GitHub Actions release pipeline,
  publish new artifacts, and update the release.

## Local verification (always allowed)

The user checks the project locally with `./run.sh`. This script only builds
locally and runs the app — it never pushes, tags, or uploads.

```bash
./run.sh                 # incremental local build + launch (Release)
./run.sh --debug         # same, Debug build
./run.sh --test          # headless self-test suite
./run.sh --no-build      # launch the existing binary
./run.sh --clean         # remove build/ before building
./run.sh --help
```

Do not substitute the local script with packaging or a release when the user
only wants to test.
