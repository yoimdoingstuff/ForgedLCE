# Building on GitHub Actions

There are two workflows:

- [standalone.yml](../.github/workflows/standalone.yml) is the everyday build. It runs on every push, needs no secrets and no game source, and builds and tests ForgedLCE against the fake game layer. See the README.
- [build-windows64.yml](../.github/workflows/build-windows64.yml) is optional. It builds the **real** Windows64 game with ForgedLCE and ExampleMod installed, and runs only when started by hand (*Actions → Build Windows64 game → Run workflow*). The rest of this page is about this one.

## What it can and can't do

- **Toolset:** GitHub's Windows runners don't include VS2012. Each run installs **VS Express 2012 for Windows Desktop** through Chocolatey (`visualstudio2012wdx`, which includes the x64 v110 compiler) and builds with VS2012's own MSBuild. This install hasn't been tried on a runner yet. If it breaks (the old Microsoft download links are the likely failure), the step fails with a clear message. You can then start a run by hand with **toolset = v143**, which uses the runner's VS2022 with legacy-compatibility flags from [ci/ci-build.props](../ci/ci-build.props). The v143 build may report errors in the game's own 2012-era code that VS2012 accepts, so treat v110 as the reference build.
- **Compile only, for now.** Every `.lib` the Windows64 game links against is a Git LFS pointer in the known copy of the source (see [RECON.md](RECON.md)). The workflow detects this and compiles both projects without linking. `Minecraft.World` builds completely, since it's a static library and needs no external libraries. `Minecraft.Client` runs the compile step only. Once the real libraries are in the source repo, the workflow links the full game automatically.
- **Windows64 only.** Console builds need the console SDKs, which GitHub-hosted runners don't have.
- **Logs only.** Build logs are uploaded as an artifact (kept 14 days). Game binaries and source are never uploaded.

## One-time setup

The LCE source is licensed and must never go into the ForgedLCE repo. CI reads it from a **separate private repo**. Before uploading it anywhere, make sure your license for the source allows hosting it on GitHub.

1. **Publish ForgedLCE to GitHub.** In GitHub Desktop: *File → Add local repository →* this folder, commit, then *Publish repository*.

2. **Publish the LCE source as a private repo.** Its files are staged but not committed yet. In GitHub Desktop: *Add local repository →* `D:\MCLCESRC`, commit everything, then *Publish repository* with **"Keep this code private" checked**. It's about 2.7 GB with no file over 100 MB, so the first push takes a while.

3. **Create a read-only token for it.** On GitHub: *Settings → Developer settings → Fine-grained personal access tokens → Generate new token*.
   - Repository access: **Only select repositories →** the LCE source repo
   - Permissions: **Contents: Read-only** (nothing else)

4. **Add two secrets to the ForgedLCE repo.** Go to *Settings → Secrets and variables → Actions → New repository secret*:

   | Name | Value |
   |---|---|
   | `LCE_SOURCE_REPO` | `your-user/your-lce-repo` |
   | `LCE_SOURCE_TOKEN` | the token from step 3 |

5. **Run it.** Go to *Actions → Build Windows64 game (needs LCE source) → Run workflow*.

## If ForgedLCE is a public repo

Anyone can read its Actions logs. The logs contain file paths and compiler messages from the game source, but not the source itself. Secrets are masked. If that's still too much exposure, make the ForgedLCE repo private as well. Private repos get 2,000 free Actions minutes a month, and a VS2012 install plus a full compile may take an hour or more per run.

## Restoring the real libraries

If you find a copy of the source with its LFS objects (`git lfs pull` succeeds there), push those objects to the private source repo. Then set `lfs: true` on the "Check out LCE source" step. The workflow will then run a full `Build` of `Minecraft.Client`, including the link step.
