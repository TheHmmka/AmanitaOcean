# Amanita Ocean — Instagram Campaign Post

Deterministic `1080 × 1080` campaign composition built from the real Current
JUCE editor render and the generated five-character ocean artwork. The layout
uses only local files and macOS system fonts; it never requests remote assets.

## Preview

Open `post.html` locally. Append `?guides=1` to show the 46-pixel square safe
margin during layout work. The guide is disabled in the exported image.

## Render

```bash
./marketing/instagram-posts/render-post.sh
```

The script uses the local Google Chrome installation, forces device scale `1`,
writes `final/amanita-ocean-campaign-post.png`, and fails unless the PNG is
exactly `1080 × 1080`. Set `CHROME_BIN` to use another Chromium-compatible
executable.

Chrome 151 on macOS may leave a headless process alive after a local-file
screenshot has already been completed. The renderer waits for the PNG to be
flushed and terminates only its isolated temporary browser profile.

## Palette

| Character | Accent |
| --- | --- |
| Default | `#81bfc7` |
| Bloom | `#c89c83` |
| Drift | `#829de0` |
| Veil | `#b3a6c4` |
| Current | `#74c6a8` |

Format copy is platform-specific: `VST3 · AU · CLAP` on macOS and
`VST3 · CLAP` on Windows/Linux.

The exact built-in image-generation brief used for the square background is
saved in [`PROMPT.md`](PROMPT.md).
