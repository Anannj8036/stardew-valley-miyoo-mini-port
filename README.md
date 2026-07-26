# Stardew Valley for Miyoo Mini

<img width="492" height="688" alt="Stardew Valley running on a Miyoo Mini" src="https://github.com/user-attachments/assets/607e7608-37ee-4dbe-b394-92888a6438b6" />

An OnionOS port of Stardew Valley for the Miyoo Mini and Miyoo Mini Plus.

Stardew Valley and its assets are not included. Version `1.6.14.24317` is the
only version tested for v1.

## Get the game files

1. Own Stardew Valley on Steam and sign in to the Steam desktop client.
2. Open `steam://open/console` in a browser.
3. Run `download_depot 413150 413151 5538941793102260869` in the Steam console.
4. Steam prints the download location when it finishes. Use the files from
   that depot folder.

## Install

1. Download the release archive and extract it.
2. Put the game files in its `gamefiles` folder.
3. Install [Mono 6](https://www.mono-project.com/download/stable/) and
   [Docker](https://docs.docker.com/get-docker/), then run `./prepare.sh` from
   the extracted folder. On Windows, use WSL.
4. Copy `OnionOS-package/Roms` to the root of the Miyoo SD card.

The copy of `prepare.sh` under [release-tools](release-tools) is here for review.
Use the one in the release archive; it needs files that are only shipped with
the release.

The port appears in OnionOS as **Stardew Valley for Miyoo Mini**.

## Source and licenses

The files written for this project are released under the [MIT License](LICENSE).
The rest of the port is not public yet.

Modified OpenAL Soft source and its build script are included under
[third_party/openal-soft](third_party/openal-soft). The release archive includes
the license notices for every bundled runtime component.
