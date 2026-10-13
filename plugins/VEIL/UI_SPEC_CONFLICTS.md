# VEIL — conflicts between the mockup and the specification

Mockup: `ui/UI_REFERENCE.png` (the new `Veil.png` skin, spec 13.6). Rule: the
specification defines behaviour, the mockup defines placement and style.
Nothing below has an invented function.

| # | Element on the mockup | Problem | Decision now | Needs approval? |
|---|---|---|---|---|
| 1 | Two filter knobs: SC HPF (top right) and HPF (bottom row) | Spec 13.6 lists only "HPF"; two knobs with the same job would be a fake control | SC HPF filters only the detector key; HPF is a high-pass in the audio path at the input, before the tube stage and before the internal detector tap. HPF is Off (20 Hz) by default | Approved by Andrew 2026-10-13 |
| 2 | MIX and DRY/WET | Could be two conflicting blends | Spec 13.6 preferred resolution: MIX = parallel compression blend inside the tube chain (after the input stage, before the output stage); DRY/WET = final blend with the original plugin input, latency-aligned. Tested as two different routings | No |
| 3 | COMPRESS readout | A dB unit would suggest a ratio or threshold | COMPRESS is the spec's macro over the vari-mu law (0–10); the readout shows the knob value without a unit | No |
| 4 | RECOVERY scale prints "400" twice | Art error | Printed numbers removed; 50 150 400 1S 2S drawn live at their real angles | No |
| 5 | COMPRESS scale prints "10" at the top | Art error (the top of a 0–10 knob is 5) | Printed numbers removed; 0 5 10 drawn live | No |
| 6 | METER GR / VU / IN/OUT | The spec names them but not what each needle shows | GR: each needle leaves 0 to the left by the dB its channel's gain element really applies. VU: output level. IN/OUT: input level, to compare with VU. 0 VU = a sine at −18 dBFS. Display only, the audio is bit-identical in every mode (tested) | No |
| 7 | DRY/WET knob drawn with two pointers | Baked art error | Both printed pointers erased; one live pointer | No |
| 8 | Lit buttons (CHARACTER VEIL, METER GR, STEREO LINK lamp, SIDECHAIN INT) are drawn slightly larger than the dark ones | Baked state | Every button is dark on the plate; the lit image (cut from the mockup, glow included) is drawn only for the real state, so a lit button may look slightly larger, as in the art | No |
| 9 | STEREO LINK lamp and DUAL MONO button | Two controls for one switch | One parameter: the lamp is lit while the channels are linked, DUAL MONO is lit while they are independent; a click on either toggles | No |
| 10 | SIDECHAIN EXT | Lit state would lie when the host feeds no key | INT is lit while the internal key is in use, EXT while an external key really arrives; EXT chosen with no key glows faintly and VEIL keeps the internal detector | No |
| 11 | Baked VU needles | Values in the art | Needles erased; live needles turn around the centre of the printed scale arc, pointing at the printed marks | No |
| 12 | Baked spectrum curve, grid and frequency labels | Values in the art | Erased; real FFT of the output, grid and labels drawn live on the analyser's own log axis | No |
| 13 | No place on the panel for the analyser quality (Off / Eco / Normal / High, addendum 13) | — | A small "FFT" button in the toolbar; stored with the plugin as a UI setting, never an audio parameter | No |
