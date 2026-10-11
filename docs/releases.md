# 🚀 GitHub Releases

Community Edition numbering restarts at **0.0.1**. Earlier 0.1.x packages and
their validation records remain historical evidence. This reset changes the
displayed/package version, without changing saves, configuration or game data.
The product remains a **development alpha**.

## 🛠️ Shared builds

[Desktop CI](../.github/workflows/build.yml) runs on branch pushes, pull requests
and manual requests. It calls the same [reusable workflow](../.github/workflows/desktop.yml)
as [releases](../.github/workflows/release.yml), building and testing:

| Runner | Target | Player download |
| --- | --- | --- |
| Ubuntu 22.04 | Linux x64 | `fta2-ce-VERSION-Linux-x86_64.zip` |
| Windows 2022 | Windows x64 | `fta2-ce-VERSION-Windows-x86_64.zip` |
| macOS 14 | Apple Silicon + Intel | `fta2-ce-VERSION-Darwin-universal.zip` |

The workflow uses the existing pinned SDL version and existing GitHub Action
major versions. Each job runs CTest without proprietary data. CPack intermediates
stay under `.cache/packages/`; `tools/release.py bundle` embeds the source ZIP,
checks the minimal runtime layout and audits the corresponding source manifest.
Only the final player ZIP is uploaded as a CI artifact, retained for 14 days.

## 🏷️ Create a release

Start with a clean working tree: commit the changes you want to release and
review `packaging/RELEASE-NOTES.md`. Then run **one** command from the project root:

| Command | Example from 1.2.3 |
| --- | --- |
| `python3 tools/release.py patch` | 1.2.4 |
| `python3 tools/release.py minor` | 1.3.0 |
| `python3 tools/release.py major` | 2.0.0 |

Each command updates the CMake version and current README version, creates a
`chore(release): vVERSION` commit and an annotated `vVERSION` tag. The application
and package versions already derive from CMake. Historical validation records
and earlier packages keep their recorded versions. Development-alpha status is
retained even for a major bump.

The commands require Python 3.10+ and Git; no configured C++ build or Node.js is
needed. Use `python` instead of `python3` if that is your Windows interpreter name.
Add `--dry-run` to preview the next version without changing files or Git. A real
bump rejects modified, staged or untracked files and existing target tags. If the
commit fails, the version files and their staging are restored. If tag creation
fails after a successful commit, the error reports that the commit already exists.

The commands do not push. To publish, run the exact command printed by the script,
for example after a patch bump from 0.0.1:

```sh
git push origin HEAD v0.0.2
```

This pushes the branch commit and that release tag, starting the release workflow.
To release the current version without increasing it, create its annotated tag
manually, for example `git tag -a v0.0.1 -m 'Community Edition 0.0.1 development alpha'`,
then push the branch and that tag with `git push origin HEAD v0.0.1`.

A pushed `v*` tag starts the release workflow. Its tag must equal `v` plus the
CMake version; mismatches stop before the platform builds. To start a manual run
for an existing tag, use `gh workflow run release.yml --ref v0.0.2`, replacing the
tag with your version. Selecting a branch fails with an explanatory message.
No personal access token is needed:
only the final publication job has `contents: write`, using `GITHUB_TOKEN`.

After all three builds pass, the publication job audits every ZIP again, verifies
that all required platform filenames are present and writes `SHA256SUMS`. It then
creates a **prerelease** with three downloadable player ZIPs and the checksum
file. The source ZIP is embedded in each runtime ZIP rather than published as
another player download. GitHub also provides its normal tag source archives.

Prereleases appear on the [Releases page](https://github.com/Celtian/fta2/releases);
they do not become a stable latest release. Existing releases/assets are not
overwritten. If a run fails before publication, fix the failure and rerun the
failed jobs. If a partial release already exists, inspect it before manually
recovering; the workflow deliberately does not use `--clobber` or delete releases.

## 🔒 Public package boundaries

Public packages contain the engine, SDL, required notices, root launcher,
`START-HERE.txt` and corresponding engine sources. They must exclude original
archives/executables, voices, saves, extracted resources and development output.
The one explicitly requested README preview, `assets/gameplay.png`, is included
in sources with an original-artwork copyright notice; it is not installed as a
loose runtime asset. Other private captures stay under `.cache/`.

The local complete Windows ZIP described in the [player guide](player-guide.md#validation-and-packaging)
contains separately verified proprietary inputs and **must never be uploaded to
GitHub Releases**. The release workflow packages a clean checkout and does not
read `dist/`, `original-game/` or local complete archives.

## 🧪 Local release checks

```sh
python3 tools/release.py version --tag v0.0.1
python3 tests/release_test.py
cpack --config build/CPackConfig.cmake -C Release -B .cache/packages
cpack --config build/CPackSourceConfig.cmake -B .cache/packages
python3 tools/release.py bundle --packages .cache/packages --output artifacts
python3 tools/release.py checksums --assets artifacts --tag v0.0.1
```

Use fresh output directories; existing ZIPs/checksums are never overwritten.
The publication job additionally passes `--all-platforms`, requiring all three
ZIPs. An automated build or successful startup does not establish a complete
playthrough or manual platform acceptance. Record actual checks and remaining
limitations in [validation](validation.md).
