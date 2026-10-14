# Third-party software

| Component | Version | Licence | Use |
|---|---|---|---|
| JUCE | 9.0.4 | AGPLv3 (open-source option) | Plugin framework, GUI, DSP utilities |
| Steinberg VST3 SDK (bundled with JUCE) | as shipped with JUCE 9.0.4 | MIT | VST3 wrapper |

Because JUCE is used under the AGPLv3, the whole of TLEET Audio is distributed
under the AGPLv3. Anyone who receives a binary is entitled to the source code
at the same version, which is published in this repository.

Build-time only (not shipped): Python 3 with OpenCV and NumPy for
`tools/skinprep/`.
