# The scripts that made the Abyss recordings

Copies, byte for byte, of the scripts under `Analyzer/Results/RevOceanCharacterization/work/abyss/` as they ran; each drives the campaign's session host with the reference's Audio Unit and needs the owner at the screen.
`probe3.py` made session D (100 probes, then the batch), `probe5.py` session E (the mode switched while probes run, then the batch, the impulse maps, a long tone, a sweep and steps of Macro), `probe4.py` session F (D's jobs again in another instance, then the structural programme with the outer controls, Macro in motion and the holdout), and `probe7_tempo.py` sessions T90 (90 BPM, play head 0.25 s ahead) and S90 (90 BPM, transport stopped).
`tempo_quicklook.py` is the first, model-free reading of a tempo session (seconds or note values, processed frames or play head).
The recordings themselves, their stimuli and each session's `info.json` stay under `Analyzer/Results/RevOceanCharacterization/work/abyss/session_<label>/`, which is not in Git; `../sessions.json` holds what a render needs beside them.
Each script writes its `session_<label>/` beside itself (`tempo_quicklook.py` reads `../session_<label>/`), so run them where the originals lie, never from this folder.
