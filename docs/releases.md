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
`chore(release): vVERSION` commit and an annotated `vVERSION` tag. It also sets
`push.followTags=true` in this repository's local Git configuration, so an ordinary
`git push` sends the release tag with the branch. Global Git settings are unchanged.
The application
and package versions already derive from CMake. Historical validation records
and earlier packages keep their recorded versions. Development-alpha status is
retained even for a major bump.

The commands require Python 3.10+ and Git; no configured C++ build or Node.js is
needed. Use `python` instead of `python3` if that is your Windows interpreter name.
Add `--dry-run` to preview the next version without changing files or Git. A real
bump rejects modified, staged or untracked files and existing target tags. If the
commit fails, the version files and their staging are restored. If tag creation
fails after a successful commit, the error reports that the commit already exists.

The commands do not push. The complete sequence for a patch release is:

```sh
python3 tools/release.py patch
git push
```

The push sends the branch commit and missing reachable annotated tags, starting
Desktop builds for the branch and GitHub release for each version tag. This Git
setting also includes older reachable annotated tags that have not yet been
pushed; existing remote tags are not replaced. A configured upstream is required
for ordinary `git push`, as with any branch push. Use `git push --no-follow-tags`
when you deliberately want branch CI followed by a manual release instead.
To release the current version without increasing it, create its annotated tag
manually, for example `git tag -a v0.0.1 -m 'Community Edition 0.0.1 development alpha'`,
then push the branch and that tag with `git push origin HEAD v0.0.1`.

A pushed `v*` tag starts the release workflow. Its tag must equal `v` plus its
source's CMake version; mismatches stop before the platform builds.

### 🖱️ Manual release from GitHub Actions

1. Push your committed branch changes, including the updated release workflow.
2. Open **Actions → 🚀 GitHub release → Run workflow**.
3. Select your branch, normally **master**. Leave **Tag** empty to use `v` plus
   that branch's CMake version, or enter an existing version tag such as `v0.0.2`.
4. Click **Run workflow**.

If the resolved tag already exists on GitHub, the workflow builds that tag's exact
commit, even if the selected branch has since advanced. If it does not exist,
the version must match the selected branch; the workflow builds that branch's
checked-out commit and creates the tag only during publication after all checks.
Every platform and the publication job use the same resolved commit, so later
branch changes cannot alter the release sources. Concurrent release workflows
are serialized. Existing releases and tags are never replaced.

The CLI equivalent is `gh workflow run release.yml --ref master`, optionally
adding `-f tag=v0.0.2` for an existing version. Manual runs on existing tag refs
also remain supported. No personal access token is needed: only the final
publication job has `contents: write`, using `GITHUB_TOKEN`.

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
