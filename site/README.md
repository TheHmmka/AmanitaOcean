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

## Analytics

Production page views and deliberate product interactions are recorded by the
self-hosted Umami instance at `https://stats.amanita.music/`. The site uses the
Ocean-specific website ID `257cbc84-9b4b-4b59-862d-b915599c4b4f`.

Tracked events cover the main hero actions, character selection, the Freeze
demonstration, each platform download, and the SHA-256 manifest. Do not reuse
this website ID for another Amanita product.

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

Release archives live under a versioned path such as `downloads/0.21.0/`.
Keep `SHA256SUMS.txt` beside the archives and never replace an already-published
version in place; publish a new versioned directory instead.

## Caddy

`deploy/ocean.Caddyfile` is a production-oriented example with compression,
cache policy, security headers, and log rotation. It serves
`ocean.amanita.music` from `/var/www/ocean.amanita.music/current`, matching the
atomic `releases/<deployment-id>` and `current` symlink layout used by the other
Amanita sites. Caddy provisions and renews HTTPS automatically.

## Refresh the plugin image

The product screenshot is rendered by the project's existing JUCE state-test
utility, so it always reflects the current editor:

```sh
./build-release/AmanitaOceanStateTests \
  --render-ui site/assets/amanita-ocean-plugin.png 4 1440 0
```

The final arguments select Current, request a 1440-point editor width, and leave
Freeze disabled. Mono Safe remains at its default Off state. The renderer writes
a `2880 × 1920` Retina-resolution PNG; keep the matching intrinsic dimensions
and a versioned cache-buster in `index.html`.
