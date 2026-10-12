# TLEET Audio

Free, open-source audio plugins modelled on classic studio hardware.
VST3 for Windows, VST3 and AU for macOS. Built with [JUCE](https://juce.com).

| Plugin | What it is | Status |
|---|---|---|
| **ASH** | Optical levelling amplifier (LA-2A family) | in progress |
| **BITE** | FET limiting amplifier (1176 family) | in progress |
| GLUE | VCA bus compressor | planned |
| PUNCH | Drum compressor | planned |
| WEIGHT | Variable-mu compressor | planned |
| VEIL | Vintage stereo compressor | planned |
| SHAPE | Passive program EQ | planned |
| ECHO | Tape echo | planned |
| VOID | Reverb | planned |
| **DOUBLE** | Stereo doubler / micro-pitch / ADT | in progress |
| STEEL | Mastering limiter | planned |
| TAPE | Tape machine | planned |

## Principles

- The DSP parameter is the single source of truth. Every meter, needle,
  curve and LED is drawn from real parameter or audio telemetry.
- Every visible control works. Controls the specification does not define
  are removed or listed in the plugin's `UI_SPEC_CONFLICTS.md`.
- Every behaviour is proven by an automated test (see `tests/`).

## Install

Download the latest build from the repository's **Actions** tab (artifact
`TLEET-windows-VST3` or `TLEET-macOS`). Installation steps are in
[BUILDING.md](BUILDING.md).

## Building from source

See [BUILDING.md](BUILDING.md).

## Licence

TLEET Audio is free software under the GNU Affero General Public License v3
(see [LICENSE](LICENSE)). It uses JUCE under its AGPLv3 option; see
[docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).

Product names of hardware mentioned in the documentation belong to their
owners and are used only to describe the sound being modelled. TLEET Audio is
not affiliated with any of them.
