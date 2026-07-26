# OpenAL Soft

The port uses OpenAL Soft 1.21.1 with local changes for lower memory use and
direct access to the Miyoo's SigmaStar audio output.

- Upstream: `https://github.com/kcat/openal-soft`
- Tag: `1.21.1`
- Commit: `ae4eacf147e2c2340cc4e02a790df04c793ed0a9`
- Archive SHA-256: `afe3f0aae719fcdc43f10d7ad7f904d53dc43718dcb1bc207e53c9ed0ba9a45a`
- License: [GNU Library General Public License 2.0](COPYING)

Build the ARMv7 library with Docker:

```sh
scripts/build-openal.sh
```

The result is written to `artifacts/openal-armhf`. The release page also carries
a source archive matching the OpenAL library in that release.
