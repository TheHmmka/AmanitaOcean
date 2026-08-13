const params = new URLSearchParams(window.location.search);
const post = document.getElementById("post");
const pluginRender = document.getElementById("pluginRender");

post.dataset.guides = params.get("guides") === "1" ? "true" : "false";

Promise.all([
  document.fonts.ready,
  pluginRender.decode().catch(() => undefined)
]).then(() => {
  document.body.dataset.ready = "true";
});
