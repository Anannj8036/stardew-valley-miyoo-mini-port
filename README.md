# Stardew Valley for Miyoo Mini

<img width="492" height="688" alt="Stardew Valley running on a Miyoo Mini" src="https://github.com/user-attachments/assets/607e7608-37ee-4dbe-b394-92888a6438b6" />

An OnionOS port of Stardew Valley for the Miyoo Mini and Miyoo Mini Plus.

Stardew Valley and its assets are not included. You need the `1.6.14.24317`
compatibility version of the game.

## Install

1. Download the release archive and extract it.
2. Put the game files in its `gamefiles` folder.
3. Install [Mono 6.12](https://www.mono-project.com/download/stable/) on your
   computer and run `prepare.sh` from the extracted folder.
4. Copy `OnionOS-package/Roms` to the root of the Miyoo SD card.

The copy of `prepare.sh` under [release-tools](release-tools) is here for review.
Use the one in the release archive; it needs files that are only shipped with
the release.

The port appears in OnionOS as **Stardew Valley for Miyoo Mini**.

## Source and licenses

The files written for this project are released under the [MIT License](LICENSE).
The rest of the port is not public yet.

Modified OpenAL Soft source and its build script are included under
[third_party/openal-soft](third_party/openal-soft). OpenAL Soft, Mono, Mono.Cecil,
MonoGame LZX, and zlib keep their own licenses; those notices are under
[third_party/notices](third_party/notices).
