# Amanita Ocean — Instagram Stories

Five deterministic `1080 × 1920` story layouts built from the real JUCE editor
renders and the generated character artwork. No remote fonts or other network
assets are used.

## Preview

Open `story.html` with a query parameter:

```text
story.html?mode=default
story.html?mode=bloom
story.html?mode=drift
story.html?mode=veil
story.html?mode=current
```

Append `&guides=1` to display the phone-safe guide during layout work. The guide
is disabled in every exported image.

## Render all five PNGs

```bash
./marketing/instagram-stories/render-stories.sh
```

The script uses the local Google Chrome installation, forces device scale `1`,
exports to `final/`, and fails if any output is not exactly `1080 × 1920`.
Set `CHROME_BIN` when using another Chromium-compatible executable.

Chrome 151 on macOS may leave a headless process alive after a local-file
screenshot is already complete. The renderer therefore uses a separate
temporary profile per Story, waits for the PNG to be flushed, and terminates
only that isolated process before moving to the next Character.

## Campaign palette

| Character | Accent | Story |
| --- | --- | --- |
| Default | `#81bfc7` | `01-amanita-ocean-default-story.png` |
| Bloom | `#c89c83` | `02-amanita-ocean-bloom-story.png` |
| Drift | `#829de0` | `03-amanita-ocean-drift-story.png` |
| Veil | `#b3a6c4` | `04-amanita-ocean-veil-story.png` |
| Current | `#74c6a8` | `05-amanita-ocean-current-story.png` |
