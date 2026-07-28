(() => {
  "use strict";

  const characterNames = ["default", "bloom", "drift", "veil", "current"];
  const root = document.documentElement;
  const canvas = document.querySelector("#ocean-canvas");
  const background =
    canvas && typeof window.OceanBackground === "function"
      ? new window.OceanBackground(canvas)
      : null;

  const characterButtons = Array.from(
    document.querySelectorAll("button[data-character]")
  );
  const characterPanels = Array.from(
    document.querySelectorAll("[data-character-panel]")
  );

  function selectCharacter(value, { moveFocus = false } = {}) {
    const parsed = Number.parseInt(value, 10);
    const index = Number.isFinite(parsed)
      ? Math.min(characterNames.length - 1, Math.max(0, parsed))
      : 0;

    root.dataset.character = characterNames[index];
    background?.setCharacter(index);

    characterButtons.forEach((button) => {
      const selected = Number.parseInt(button.dataset.character, 10) === index;
      button.classList.toggle("is-active", selected);
      button.setAttribute("aria-selected", String(selected));
      button.tabIndex = selected ? 0 : -1;

      if (selected && moveFocus) {
        button.focus({ preventScroll: true });
      }
    });

    characterPanels.forEach((panel) => {
      const selected =
        Number.parseInt(panel.dataset.characterPanel, 10) === index;
      panel.hidden = !selected;
      panel.classList.toggle("is-active", selected);
    });
  }

  characterButtons.forEach((button) => {
    button.addEventListener("click", () => {
      selectCharacter(button.dataset.character);
    });

    button.addEventListener("keydown", (event) => {
      const activeIndex = characterButtons.indexOf(button);
      let nextIndex = activeIndex;

      if (event.key === "ArrowRight" || event.key === "ArrowDown") {
        nextIndex = (activeIndex + 1) % characterButtons.length;
      } else if (event.key === "ArrowLeft" || event.key === "ArrowUp") {
        nextIndex =
          (activeIndex - 1 + characterButtons.length) %
          characterButtons.length;
      } else if (event.key === "Home") {
        nextIndex = 0;
      } else if (event.key === "End") {
        nextIndex = characterButtons.length - 1;
      } else {
        return;
      }

      event.preventDefault();
      selectCharacter(
        characterButtons[nextIndex]?.dataset.character ?? nextIndex,
        { moveFocus: true }
      );
    });
  });

  function bindOceanControl(control) {
    const outputId = control.getAttribute("aria-describedby");
    const output = outputId ? document.querySelector(`#${outputId}`) : null;
    const name = control.dataset.oceanControl;

    const update = () => {
      const min = Number.parseFloat(control.min || "0");
      const max = Number.parseFloat(control.max || "100");
      const numericValue = Number.parseFloat(control.value || "0");
      const normalised =
        max > min ? Math.min(1, Math.max(0, (numericValue - min) / (max - min))) : 0;

      control.style.setProperty("--control-value", `${normalised * 100}%`);

      if (output) {
        output.textContent = `${Math.round(numericValue)}%`;
      }

      if (name === "evolution") {
        background?.setEvolution(normalised);
      } else if (name === "focus") {
        background?.setFocus(normalised);
      }
    };

    control.addEventListener("input", update);
    update();
  }

  document
    .querySelectorAll("[data-ocean-control]")
    .forEach(bindOceanControl);

  const freezeControl = document.querySelector("[data-ocean-freeze]");
  if (freezeControl) {
    freezeControl.addEventListener("click", () => {
      const active = freezeControl.getAttribute("aria-pressed") !== "true";
      freezeControl.setAttribute("aria-pressed", String(active));
      freezeControl.classList.toggle("is-active", active);
      background?.setFreeze(active);
    });
  }

  const revealItems = document.querySelectorAll("[data-reveal]");
  if (
    revealItems.length > 0 &&
    "IntersectionObserver" in window &&
    !window.matchMedia("(prefers-reduced-motion: reduce)").matches
  ) {
    root.classList.add("reveal-ready");
    const observer = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (!entry.isIntersecting) {
            return;
          }

          entry.target.classList.add("is-revealed");
          observer.unobserve(entry.target);
        });
      },
      { rootMargin: "0px 0px -8% 0px", threshold: 0.12 }
    );

    revealItems.forEach((item) => observer.observe(item));
  } else {
    revealItems.forEach((item) => item.classList.add("is-revealed"));
  }

  const year = document.querySelector("[data-current-year]");
  if (year) {
    year.textContent = String(new Date().getFullYear());
  }

  selectCharacter(
    characterButtons.find((button) => button.classList.contains("is-active"))
      ?.dataset.character ?? 0
  );

  window.addEventListener(
    "pagehide",
    () => {
      background?.destroy();
    },
    { once: true }
  );
})();
