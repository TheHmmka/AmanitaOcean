# Amanita Ocean website

The product site is a framework-free static bundle: semantic HTML, CSS, vanilla
JavaScript, and a WebGL2 background. It does not require a build step and can be
uploaded directly to any static web root.

## Local preview

Run from the repository root:

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory site
```

Then open <http://127.0.0.1:4173/>.

Use an HTTP server rather than opening `index.html` directly so browser security
rules match production. WebGL2 is used when available; the page retains a CSS
fallback and readable content when graphics acceleration is unavailable or
reduced motion is requested.

## Upload

The production site is published at <https://ocean.amanita.music/>. Upload the
**contents** of `site/` to the configured document root and keep the relative
layout intact:

```text
index.html
styles.css
robots.txt
assets/
downloads/
js/
```

Release archives live under a versioned path such as `downloads/0.20.0/`.
Keep `SHA256SUMS.txt` beside the archives and never replace an already-published
version in place; publish a new versioned directory instead.

## Caddy

`deploy/ocean.Caddyfile` is a production-oriented example with compression,
cache policy, security headers, and log rotation. It serves
`ocean.amanita.music` from `/var/www/ocean.amanita.music/current`, matching the
atomic `releases/<version>` and `current` symlink layout used by the other
Amanita sites. Caddy provisions and renews HTTPS automatically.

## Refresh the plugin image

The product screenshot is rendered by the project's existing JUCE state-test
utility, so it always reflects the current editor:

```sh
./build-release/AmanitaOceanStateTests \
  --render-ui site/assets/amanita-ocean-plugin.png 0 1440 0
```

The final arguments select Default, request a 1440-point editor width, and leave
Freeze disabled. Mono Safe remains at its default Off state. The renderer writes
a `2880 × 1920` Retina-resolution PNG; keep the matching intrinsic dimensions
and a versioned cache-buster in `index.html`.
