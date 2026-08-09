const characters = {
  default: {
    index: "01 / 05",
    accent: "#81bfc7",
    accentRgb: "129, 191, 199",
    descriptor: "PURE / OPEN",
    title: "DEFAULT",
    headline: "Space without a signature.",
    body: "The balanced eight-line FDN stays clear while ultra-slow fractional-delay motion keeps the tail alive.",
    signature: "FRACTIONAL-DELAY MOTION",
    background: "backgrounds/01-default.png",
    plugin: "plugin-renders/default.png"
  },
  bloom: {
    index: "02 / 05",
    accent: "#c89c83",
    accentRgb: "200, 156, 131",
    descriptor: "RISING / DIFFUSION",
    title: "BLOOM",
    headline: "The tail rises after the sound.",
    body: "A causal rising-tap cluster swells into a second stereo diffusion layer—soft, expansive and never granular.",
    signature: "CAUSAL SWELL + AP4 DIFFUSION",
    background: "backgrounds/02-bloom.png",
    plugin: "plugin-renders/bloom.png"
  },
  drift: {
    index: "03 / 05",
    accent: "#829de0",
    accentRgb: "130, 157, 224",
    descriptor: "SPECTRAL / MOTION",
    title: "DRIFT",
    headline: "Colour moves inside the tail.",
    body: "Passive spectral kernels travel through the feedback loop itself, without filtering the finished wet signal.",
    signature: "IN-LOOP SPECTRAL MOVEMENT",
    background: "backgrounds/03-drift.png",
    plugin: "plugin-renders/drift.png"
  },
  veil: {
    index: "04 / 05",
    accent: "#b3a6c4",
    accentRgb: "179, 166, 196",
    descriptor: "SOFT / CLOUD",
    title: "VEIL",
    headline: "The attack becomes atmosphere.",
    body: "Six all-pass stages redistribute transient energy into a soft cloud—without lookahead, pumping or granular windows.",
    signature: "SIX-STAGE ALL-PASS CLOUD",
    background: "backgrounds/04-veil.png",
    plugin: "plugin-renders/veil.png"
  },
  current: {
    index: "05 / 05",
    accent: "#74c6a8",
    accentRgb: "116, 198, 168",
    descriptor: "COHERENT / FLOW",
    title: "CURRENT",
    headline: "The room moves as one.",
    body: "One coherent field steers delay, damping and full stereo position across all eight lines—movement without a chorus.",
    signature: "SHARED RANK-2 CURRENT FIELD",
    background: "backgrounds/05-current.png",
    plugin: "plugin-renders/current.png"
  }
};

const params = new URLSearchParams(window.location.search);
const requestedMode = (params.get("mode") || "default").toLowerCase();
const mode = Object.hasOwn(characters, requestedMode) ? requestedMode : "default";
const character = characters[mode];

const root = document.documentElement;
const story = document.getElementById("story");
root.style.setProperty("--accent", character.accent);
root.style.setProperty("--accent-rgb", character.accentRgb);
story.dataset.mode = mode;
story.dataset.guides = params.get("guides") === "1" ? "true" : "false";

document.getElementById("background").style.backgroundImage = `url("${character.background}")`;
document.getElementById("seriesCount").textContent = character.index;
document.getElementById("descriptor").textContent = character.descriptor;
document.getElementById("modeTitle").textContent = character.title;
document.getElementById("headline").textContent = character.headline;
document.getElementById("bodyCopy").textContent = character.body;
document.getElementById("signature").textContent = character.signature;

const pluginRender = document.getElementById("pluginRender");
pluginRender.src = character.plugin;
pluginRender.alt = `Amanita Ocean plug-in showing the ${character.title} character`;

document.title = `Amanita Ocean — ${character.title} Instagram Story`;

Promise.all([
  document.fonts.ready,
  pluginRender.decode().catch(() => undefined)
]).then(() => {
  document.body.dataset.ready = "true";
});
