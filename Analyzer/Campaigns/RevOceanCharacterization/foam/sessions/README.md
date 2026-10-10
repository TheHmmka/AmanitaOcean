# The scripts that made the Foam recordings

Copies, byte for byte, of the scripts under `Analyzer/Results/RevOceanCharacterization/work/foam/` as they ran; each drives the campaign's session host with the reference's Audio Unit and needs the owner at the screen.
`probe3.py` made session A (120 BPM, play head at the frame count: 100 probes while the owner switched the mode to Foam, then the batch, the impulse maps, a long tone, a sweep and eleven steps of Macro).
`probe_foam2.py` made session T90 (90 BPM, play head 0.25 s ahead, another instance: probes until Foam is heard, the trains f01 and f02, the time scan f03, the tone f04 on four block grids, the outer controls k00 to k16, Macro in motion m01, the noise n01 and n02, an end probe).
The recordings themselves, their stimuli and each session's `info.json` stay under `Analyzer/Results/RevOceanCharacterization/work/foam/session_<label>/`, which is not in Git; `../sessions.json` holds what a render needs beside them.
Each script writes its `session_<label>/` beside itself, so run them where the originals lie, never from this folder.
