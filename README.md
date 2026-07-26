# Stardew Valley for Miyoo Mini

<img width="492" height="688" alt="Stardew Valley running on a Miyoo Mini" src="https://github.com/user-attachments/assets/607e7608-37ee-4dbe-b394-92888a6438b6" />

An OnionOS port of Stardew Valley for the Miyoo Mini and Miyoo Mini Plus.

This repository contains release information, the preparation scripts included
with the download, and source and notices for third-party components. The port
itself is not open source yet. Stardew Valley and its assets are not included.

## Install

1. Download `stardew-valley-miyoo-mini-v1.0.0.tar.gz` from GitHub Releases.
2. Extract it and put the Stardew Valley `1.6.14.24317` compatibility files in
   the `gamefiles` folder.
3. On your computer, install [Mono 6.12](https://www.mono-project.com/download/stable/),
   then run `prepare.sh` from the extracted release folder.
4. Copy `OnionOS-package/Roms` to the root of the Miyoo SD card.

`prepare.sh` is in the root of the downloaded release. The exact script and its
supporting shell scripts are also mirrored in [release-tools](release-tools) if
you would like to review them. Run the copy in the release archive because it
needs the compiled preparation tools and runtime template shipped beside it.

The port is installed as **Stardew Valley for Miyoo Mini**.
Game files and saves are not included in the download.

## Third-party Source

The modified OpenAL Soft source and the exact build scripts used for the bundled
audio library are under [third_party/openal-soft](third_party/openal-soft) and
[scripts](scripts). A matching OpenAL source archive is also attached to every
GitHub release.

Other bundled components keep their own licenses. Their notices and source
references are under [third_party/notices](third_party/notices).
