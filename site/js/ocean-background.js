(() => {
    "use strict";

    const root = document.documentElement;
    const characterAccents = [
        [0x81 / 255, 0xbf / 255, 0xc7 / 255],
        [0xc8 / 255, 0x9c / 255, 0x83 / 255],
        [0x82 / 255, 0x9d / 255, 0xe0 / 255],
        [0xb3 / 255, 0xa6 / 255, 0xc4 / 255],
        [0x74 / 255, 0xc6 / 255, 0xa8 / 255],
    ];

    const vertexShaderSource = `#version 300 es
precision highp float;

out vec2 vUv;

void main()
{
    vec2 point = vec2(float((gl_VertexID << 1) & 2),
                      float(gl_VertexID & 2));
    vUv = point;
    gl_Position = vec4(point * 2.0 - 1.0, 0.0, 1.0);
}
`;

    const sceneShaderSource = `#version 300 es
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform float uEvolution;
uniform float uFocus;
uniform float uDirectOutput;
uniform vec3 uAccent;
uniform vec4 uCharacterBlend;
uniform float uCurrentBlend;
uniform vec2 uCurrentFlow;
uniform float uCurrentStrength;

float hash21(vec2 p)
{
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float valueNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 local = fract(p);
    local = local * local * local * (local * (local * 6.0 - 15.0) + 10.0);

    float a = hash21(cell);
    float b = hash21(cell + vec2(1.0, 0.0));
    float c = hash21(cell + vec2(0.0, 1.0));
    float d = hash21(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, local.x), mix(c, d, local.x), local.y);
}

float flowFbm(vec2 p)
{
    const mat2 octaveRotation = mat2(0.80, -0.60, 0.60, 0.80);
    float result = valueNoise(p) * 0.57;
    p = octaveRotation * p * 2.07 + vec2(13.17, 7.91);
    result += valueNoise(p) * 0.27;
    p = octaveRotation * p * 2.07 + vec2(-5.73, 11.29);
    result += valueNoise(p) * 0.13;
    return result * 1.031;
}

void main()
{
    vec2 resolution = max(uResolution, vec2(1.0));
    vec2 uv = clamp(vUv, 0.0, 1.0);
    vec2 p = uv - 0.5;
    p.x *= resolution.x / resolution.y;

    float evolution = clamp(uEvolution, 0.0, 1.0);
    float focus = clamp(uFocus, 0.0, 1.0);
    vec4 character = max(uCharacterBlend, vec4(0.0));
    float currentBlend = max(uCurrentBlend, 0.0);
    float characterWeight = max(dot(character, vec4(1.0))
                                + currentBlend,
                                0.0001);
    character /= characterWeight;
    currentBlend /= characterWeight;

    vec2 currentVector = clamp(uCurrentFlow, vec2(-1.0), vec2(1.0));
    float currentDepth = currentBlend
                       * clamp(uCurrentStrength, 0.0, 1.0);

    float scaleFactor = (dot(character, vec4(1.00, 0.78, 1.08, 0.84))
                      + currentBlend * 0.94)
                      * mix(0.97, 1.05, evolution);
    vec2 anisotropy;
    anisotropy.x = dot(character, vec4(1.00, 1.00, 1.35, 0.76))
                 + currentBlend * 1.48;
    anisotropy.y = dot(character, vec4(1.00, 1.08, 0.78, 1.32))
                 + currentBlend * 0.72;
    float warpFactor = dot(character, vec4(1.00, 0.90, 1.08, 1.20))
                     + currentBlend * 1.28;
    float speedFactor = dot(character, vec4(0.72, 0.54, 1.24, 0.46))
                      + currentBlend * 0.82;
    float densityFactor = dot(character, vec4(1.15, 1.28, 1.08, 0.88))
                        + currentBlend * 1.12;
    float maskFactor = dot(character, vec4(0.88, 1.18, 1.02, 0.58))
                     + currentBlend * 0.90;
    float time = uTime * speedFactor * mix(0.68, 1.12, evolution);

    vec2 q = p * anisotropy * scaleFactor;
    q += currentVector * currentDepth * 0.16;
    vec2 warp;
    warp.x = flowFbm(q * 0.78
                     + time * vec2(0.008, -0.006));
    warp.y = flowFbm(q * 0.78 + vec2(7.31, -4.17)
                     + time * vec2(-0.006, 0.009));
    warp = warp * 2.0 - 1.0;
    warp += currentDepth
          * vec2(currentVector.y, -currentVector.x) * 0.12;

    vec2 flow = q
              + warp * mix(0.34, 0.76, evolution) * warpFactor
              + time * vec2(-0.006, 0.004);
    flow += currentDepth
          * (currentVector * 0.22 + vec2(-p.y, p.x) * 0.06);
    float body = flowFbm(flow * 0.94);
    float undercurrent = flowFbm(
        mat2(0.78, -0.63, 0.63, 0.78) * flow * 1.46
        - warp * 0.42
        + time * vec2(0.010, -0.013));
    float detail = valueNoise(
        flow * 1.90 + warp.yx * 0.30
        + time * vec2(-0.016, 0.011));

    float farSignal = body * 0.72 + undercurrent * 0.28;
    float farDensity = smoothstep(0.33, 0.66, farSignal);
    float midSignal = body * 0.48 + undercurrent * 0.52;
    float midDensity = smoothstep(0.37, 0.68, midSignal);
    float nearSignal = undercurrent * 0.66 + detail * 0.34;
    float nearDensity = smoothstep(mix(0.57, 0.50, evolution),
                                   mix(0.79, 0.72, evolution),
                                   nearSignal)
                      * smoothstep(0.10, 0.55, midDensity);
    farDensity = pow(farDensity, mix(0.96, 1.02, focus));
    midDensity = pow(midDensity, mix(0.94, 1.05, focus));
    nearDensity = pow(nearDensity, mix(0.92, 1.08, focus));

    vec3 accent = clamp(uAccent, vec3(0.0), vec3(1.0));
    float luminance = dot(accent, vec3(0.2126, 0.7152, 0.0722));
    accent = mix(vec3(luminance), accent, 0.72);

    vec3 deepTop = vec3(0.014, 0.043, 0.050);
    vec3 deepBottom = vec3(0.004, 0.015, 0.020);
    vec3 colour = mix(deepBottom, deepTop, pow(1.0 - uv.y, 0.74));
    vec3 farTint = mix(vec3(0.010, 0.040, 0.048), accent * 0.130, 0.35);
    vec3 midTint = mix(vec3(0.014, 0.052, 0.062), accent * 0.190, 0.55);
    vec3 nearTint = mix(vec3(0.018, 0.062, 0.072), accent * 0.250, 0.68);
    colour += farTint * farDensity
            * mix(0.72, 1.08, evolution) * densityFactor;
    colour += midTint * midDensity
            * mix(0.80, 1.24, evolution) * densityFactor;
    colour += nearTint * nearDensity
            * mix(0.58, 1.12, evolution) * densityFactor;
    colour *= 0.92;

    float vignette = 1.0 - smoothstep(
        0.52, 1.04, length(p * vec2(0.72, 1.10)));
    colour *= mix(0.96, 1.0, vignette);

    float peak = undercurrent * 0.64 + detail * 0.36;
    float core = smoothstep(mix(0.70, 0.63, evolution),
                            mix(0.84, 0.77, evolution),
                            peak);
    float gate = smoothstep(0.45, 0.64, body + warp.x * 0.06);
    float softCurrent = smoothstep(0.48, 0.72,
                                   undercurrent * 0.62 + body * 0.38)
                      * smoothstep(0.10, 0.64, midDensity);
    float emission = max(pow(core * gate, 1.08),
                         softCurrent * mix(0.18, 0.34, evolution))
                   * maskFactor * mix(0.58, 1.10, evolution);

    colour /= vec3(1.0) + colour * 0.30;
    colour = pow(max(colour, vec3(0.0)), vec3(0.82));
    float outputAlpha = mix(clamp(emission, 0.0, 1.0),
                            1.0,
                            clamp(uDirectOutput, 0.0, 1.0));
    fragColor = vec4(colour, outputAlpha);
}
`;

    const blurShaderSource = `#version 300 es
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uSource;
uniform vec2 uDirection;

float maskAt(vec2 offset)
{
    return texture(uSource, clamp(vUv + offset, 0.0, 1.0)).a;
}

void main()
{
    float blurred = maskAt(vec2(0.0)) * 0.227027;
    blurred += (maskAt(uDirection * 1.384615)
                + maskAt(-uDirection * 1.384615)) * 0.316216;
    blurred += (maskAt(uDirection * 3.230769)
                + maskAt(-uDirection * 3.230769)) * 0.070270;
    fragColor = vec4(0.0, 0.0, 0.0, blurred);
}
`;

    const compositeShaderSource = `#version 300 es
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uEvolution;
uniform float uFocus;
uniform vec3 uAccent;
uniform vec4 uCharacterBlend;
uniform float uCurrentBlend;

void main()
{
    vec2 uv = clamp(vUv, 0.0, 1.0);
    vec4 scene = texture(uScene, uv);
    float halo = texture(uBloom, uv).a;
    float evolution = clamp(uEvolution, 0.0, 1.0);
    float focus = clamp(uFocus, 0.0, 1.0);
    vec4 character = max(uCharacterBlend, vec4(0.0));
    float currentBlend = max(uCurrentBlend, 0.0);
    float characterWeight = max(dot(character, vec4(1.0))
                                + currentBlend,
                                0.0001);
    character /= characterWeight;
    currentBlend /= characterWeight;

    vec3 accent = clamp(uAccent, vec3(0.0), vec3(1.0));
    float luminance = dot(accent, vec3(0.2126, 0.7152, 0.0722));
    accent = mix(vec3(luminance), accent, 0.74);
    vec3 glowTint = mix(vec3(0.040, 0.125, 0.138), accent, 0.70);

    float coreGain = (dot(character, vec4(0.068, 0.086, 0.081, 0.041))
                    + currentBlend * 0.052)
                   * mix(0.55, 1.15, evolution)
                   * mix(0.88, 1.12, focus);
    float haloGain = (dot(character, vec4(0.44, 0.63, 0.49, 0.55))
                    + currentBlend * 0.56)
                   * mix(0.68, 1.25, evolution)
                   * mix(1.12, 1.00, focus);
    float light = scene.a * coreGain
                + halo * haloGain * uBloomStrength;

    vec3 colour = scene.rgb + glowTint * light;
    colour /= vec3(1.0) + colour * 0.18;
    fragColor = vec4(colour, 1.0);
}
`;

    const clampUnit = (value) => Number.isFinite(value)
        ? Math.min(1, Math.max(0, value))
        : 0;

    const smoothingAmount = (elapsedSeconds, timeConstantSeconds) =>
        elapsedSeconds > 0
            ? 1 - Math.exp(-elapsedSeconds / timeConstantSeconds)
            : 0;

    class OceanBackground {
        constructor(canvas) {
            this.canvas = typeof canvas === "string"
                ? document.querySelector(canvas)
                : canvas;
            this.gl = null;
            this.programs = null;
            this.vertexArray = null;
            this.targets = null;
            this.resizeObserver = null;
            this.frameRequest = 0;
            this.lastFrameSeconds = 0;
            this.animationTime = 19.73;
            this.currentFieldTime = 19.73;
            this.motion = 1;
            this.destroyed = false;
            this.failed = false;
            this.failureReported = false;
            this.resizePending = true;
            this.outputWidth = 0;
            this.outputHeight = 0;

            this.targetCharacter = 0;
            this.targetEvolution = 0.35;
            this.targetFocus = 1;
            this.targetFrozen = false;
            this.characterBlend = new Float32Array([1, 0, 0, 0, 0]);
            this.legacyCharacterBlend = new Float32Array([1, 0, 0, 0]);
            this.renderedEvolution = this.targetEvolution;
            this.renderedFocus = this.targetFocus;
            this.targetCurrentFlow = new Float32Array(2);
            this.renderedCurrentFlow = new Float32Array(2);
            this.targetCurrentStrength = 0;
            this.renderedCurrentStrength = 0;
            this.renderedAccent = new Float32Array(characterAccents[0]);

            this.reducedMotionQuery = window.matchMedia
                ? window.matchMedia("(prefers-reduced-motion: reduce)")
                : null;
            this.reducedMotion = Boolean(
                this.reducedMotionQuery && this.reducedMotionQuery.matches
            );

            this.onFrame = (time) => this.renderFrame(time);
            this.onResize = () => {
                this.resizePending = true;
                this.requestFrame();
            };
            this.onVisibilityChange = () => {
                if (document.hidden) {
                    this.cancelFrame();
                    return;
                }

                this.lastFrameSeconds = 0;
                this.requestFrame();
            };
            this.onReducedMotionChange = (event) => {
                this.reducedMotion = event.matches;
                this.lastFrameSeconds = 0;
                if (this.reducedMotion) {
                    this.cancelFrame();
                }
                this.requestFrame();
            };
            this.onContextLost = (event) => {
                event.preventDefault();
                this.cancelFrame();
                this.fail(new Error("WebGL context was lost."));
            };
            this.onContextRestored = () => {
                if (this.destroyed) {
                    return;
                }

                try {
                    this.failed = false;
                    this.failureReported = false;
                    this.createResources();
                    this.resizePending = true;
                    root.classList.remove("ocean-webgl-fallback");
                    root.classList.add("ocean-webgl-ready");
                    this.canvas.classList.remove("is-fallback");
                    this.canvas.hidden = false;
                    this.lastFrameSeconds = 0;
                    this.requestFrame();
                } catch (error) {
                    this.fail(error);
                }
            };

            if (!(this.canvas instanceof HTMLCanvasElement)) {
                this.fail(new Error(
                    "OceanBackground requires the #ocean-canvas element."
                ));
                return;
            }

            this.canvas.setAttribute("aria-hidden", "true");
            this.canvas.style.position ||= "fixed";
            this.canvas.style.inset ||= "0";
            this.canvas.style.width ||= "100%";
            this.canvas.style.height ||= "100%";
            this.canvas.style.pointerEvents ||= "none";

            try {
                this.gl = this.canvas.getContext("webgl2", {
                    alpha: false,
                    antialias: false,
                    depth: false,
                    stencil: false,
                    premultipliedAlpha: false,
                    preserveDrawingBuffer: false,
                    powerPreference: "high-performance",
                });

                if (!this.gl) {
                    throw new Error("WebGL2 is not available.");
                }

                this.canvas.addEventListener(
                    "webglcontextlost",
                    this.onContextLost,
                    false
                );
                this.canvas.addEventListener(
                    "webglcontextrestored",
                    this.onContextRestored,
                    false
                );
                window.addEventListener("resize", this.onResize, {
                    passive: true,
                });
                document.addEventListener(
                    "visibilitychange",
                    this.onVisibilityChange
                );

                if (this.reducedMotionQuery) {
                    if (this.reducedMotionQuery.addEventListener) {
                        this.reducedMotionQuery.addEventListener(
                            "change",
                            this.onReducedMotionChange
                        );
                    } else {
                        this.reducedMotionQuery.addListener(
                            this.onReducedMotionChange
                        );
                    }
                }

                if ("ResizeObserver" in window) {
                    this.resizeObserver = new ResizeObserver(this.onResize);
                    this.resizeObserver.observe(this.canvas);
                }

                this.createResources();
                root.classList.remove("ocean-webgl-fallback");
                root.classList.add("ocean-webgl-ready");
                this.canvas.hidden = false;
                this.requestFrame();
            } catch (error) {
                this.fail(error);
            }
        }

        setCharacter(index) {
            const numericIndex = Number(index);
            this.targetCharacter = Number.isFinite(numericIndex)
                ? Math.min(4, Math.max(0, Math.round(numericIndex)))
                : 0;
            this.requestFrame();
            return this;
        }

        setEvolution(value) {
            this.targetEvolution = clampUnit(Number(value));
            this.requestFrame();
            return this;
        }

        setFocus(value) {
            this.targetFocus = clampUnit(Number(value));
            this.requestFrame();
            return this;
        }

        setFreeze(frozen) {
            this.targetFrozen = Boolean(frozen);
            this.requestFrame();
            return this;
        }

        destroy() {
            if (this.destroyed) {
                return;
            }

            this.destroyed = true;
            this.cancelFrame();
            this.resizeObserver?.disconnect();
            this.resizeObserver = null;

            window.removeEventListener("resize", this.onResize);
            document.removeEventListener(
                "visibilitychange",
                this.onVisibilityChange
            );

            if (this.reducedMotionQuery) {
                if (this.reducedMotionQuery.removeEventListener) {
                    this.reducedMotionQuery.removeEventListener(
                        "change",
                        this.onReducedMotionChange
                    );
                } else {
                    this.reducedMotionQuery.removeListener(
                        this.onReducedMotionChange
                    );
                }
            }

            if (this.canvas) {
                this.canvas.removeEventListener(
                    "webglcontextlost",
                    this.onContextLost,
                    false
                );
                this.canvas.removeEventListener(
                    "webglcontextrestored",
                    this.onContextRestored,
                    false
                );
            }

            this.deleteResources();
            root.classList.remove("ocean-webgl-ready");
        }

        createResources() {
            const gl = this.gl;
            if (!gl) {
                throw new Error("Cannot create shader resources without WebGL2.");
            }

            this.deleteResources();
            this.programs = {
                scene: this.createProgram(
                    sceneShaderSource,
                    "Abyssal Flow scene"
                ),
                blur: this.createProgram(
                    blurShaderSource,
                    "Abyssal Flow blur"
                ),
                composite: this.createProgram(
                    compositeShaderSource,
                    "Abyssal Flow composite"
                ),
            };
            this.vertexArray = gl.createVertexArray();
            if (!this.vertexArray) {
                throw new Error("Unable to create the fullscreen triangle VAO.");
            }

            gl.disable(gl.DEPTH_TEST);
            gl.disable(gl.SCISSOR_TEST);
            gl.disable(gl.CULL_FACE);
            gl.disable(gl.BLEND);
        }

        createProgram(fragmentSource, label) {
            const gl = this.gl;
            const vertexShader = this.compileShader(
                gl.VERTEX_SHADER,
                vertexShaderSource,
                `${label} vertex`
            );
            const fragmentShader = this.compileShader(
                gl.FRAGMENT_SHADER,
                fragmentSource,
                `${label} fragment`
            );
            const program = gl.createProgram();

            if (!program) {
                gl.deleteShader(vertexShader);
                gl.deleteShader(fragmentShader);
                throw new Error(`Unable to create ${label} program.`);
            }

            gl.attachShader(program, vertexShader);
            gl.attachShader(program, fragmentShader);
            gl.linkProgram(program);
            gl.deleteShader(vertexShader);
            gl.deleteShader(fragmentShader);

            if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
                const details = gl.getProgramInfoLog(program) || "unknown error";
                gl.deleteProgram(program);
                throw new Error(`${label} link failed: ${details}`);
            }

            return program;
        }

        compileShader(type, source, label) {
            const gl = this.gl;
            const shader = gl.createShader(type);
            if (!shader) {
                throw new Error(`Unable to create ${label} shader.`);
            }

            gl.shaderSource(shader, source);
            gl.compileShader(shader);
            if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
                const details = gl.getShaderInfoLog(shader) || "unknown error";
                gl.deleteShader(shader);
                throw new Error(`${label} compilation failed: ${details}`);
            }

            return shader;
        }

        createRenderTarget(width, height) {
            const gl = this.gl;
            const texture = gl.createTexture();
            const framebuffer = gl.createFramebuffer();
            if (!texture || !framebuffer) {
                if (texture) {
                    gl.deleteTexture(texture);
                }
                if (framebuffer) {
                    gl.deleteFramebuffer(framebuffer);
                }
                throw new Error("Unable to create an Ocean render target.");
            }

            gl.bindTexture(gl.TEXTURE_2D, texture);
            gl.texParameteri(
                gl.TEXTURE_2D,
                gl.TEXTURE_MIN_FILTER,
                gl.LINEAR
            );
            gl.texParameteri(
                gl.TEXTURE_2D,
                gl.TEXTURE_MAG_FILTER,
                gl.LINEAR
            );
            gl.texParameteri(
                gl.TEXTURE_2D,
                gl.TEXTURE_WRAP_S,
                gl.CLAMP_TO_EDGE
            );
            gl.texParameteri(
                gl.TEXTURE_2D,
                gl.TEXTURE_WRAP_T,
                gl.CLAMP_TO_EDGE
            );
            gl.texImage2D(
                gl.TEXTURE_2D,
                0,
                gl.RGBA8,
                width,
                height,
                0,
                gl.RGBA,
                gl.UNSIGNED_BYTE,
                null
            );

            gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
            gl.framebufferTexture2D(
                gl.FRAMEBUFFER,
                gl.COLOR_ATTACHMENT0,
                gl.TEXTURE_2D,
                texture,
                0
            );
            const complete = gl.checkFramebufferStatus(gl.FRAMEBUFFER)
                === gl.FRAMEBUFFER_COMPLETE;
            gl.bindFramebuffer(gl.FRAMEBUFFER, null);
            gl.bindTexture(gl.TEXTURE_2D, null);

            if (!complete) {
                gl.deleteFramebuffer(framebuffer);
                gl.deleteTexture(texture);
                throw new Error("Ocean render target is incomplete.");
            }

            return { framebuffer, texture, width, height };
        }

        resize() {
            const gl = this.gl;
            const bounds = this.canvas.getBoundingClientRect();
            const cssWidth = Math.max(
                1,
                Math.round(bounds.width || window.innerWidth || 1)
            );
            const cssHeight = Math.max(
                1,
                Math.round(bounds.height || window.innerHeight || 1)
            );
            const deviceScale = Math.min(
                2,
                Math.max(1, window.devicePixelRatio || 1)
            );
            const outputWidth = Math.max(
                1,
                Math.round(cssWidth * deviceScale)
            );
            const outputHeight = Math.max(
                1,
                Math.round(cssHeight * deviceScale)
            );

            const targetsMatch = this.targets
                && this.outputWidth === outputWidth
                && this.outputHeight === outputHeight;
            if (
                targetsMatch
                && this.canvas.width === outputWidth
                && this.canvas.height === outputHeight
            ) {
                this.resizePending = false;
                return;
            }

            this.canvas.width = outputWidth;
            this.canvas.height = outputHeight;
            this.outputWidth = outputWidth;
            this.outputHeight = outputHeight;

            const baseScale = deviceScale > 1.25 ? 0.60 : 1;
            const fitScale = Math.min(
                baseScale,
                1600 / outputWidth,
                1000 / outputHeight
            );
            const sceneWidth = Math.max(
                1,
                Math.round(outputWidth * fitScale)
            );
            const sceneHeight = Math.max(
                1,
                Math.round(outputHeight * fitScale)
            );
            const bloomWidth = Math.max(32, Math.floor((sceneWidth + 1) / 2));
            const bloomHeight = Math.max(
                32,
                Math.floor((sceneHeight + 1) / 2)
            );

            this.deleteTargets();
            this.targets = {
                scene: this.createRenderTarget(sceneWidth, sceneHeight),
                horizontal: this.createRenderTarget(
                    bloomWidth,
                    bloomHeight
                ),
                vertical: this.createRenderTarget(bloomWidth, bloomHeight),
            };
            gl.bindFramebuffer(gl.FRAMEBUFFER, null);
            this.resizePending = false;
        }

        renderFrame(timeMilliseconds) {
            this.frameRequest = 0;
            if (
                this.destroyed
                || this.failed
                || document.hidden
                || !this.gl
            ) {
                return;
            }

            try {
                if (this.resizePending || !this.targets) {
                    this.resize();
                }

                const nowSeconds = timeMilliseconds * 0.001;
                const elapsedSeconds = this.lastFrameSeconds > 0
                    ? Math.min(
                        0.10,
                        Math.max(0, nowSeconds - this.lastFrameSeconds)
                    )
                    : 1 / 30;
                this.lastFrameSeconds = nowSeconds;

                if (this.reducedMotion) {
                    this.updateCurrentFieldTargets();
                    this.snapToTargets();
                } else {
                    this.currentFieldTime += elapsedSeconds;
                    if (!Number.isFinite(this.currentFieldTime)) {
                        this.currentFieldTime = 19.73;
                    } else if (this.currentFieldTime >= 4096) {
                        this.currentFieldTime %= 4096;
                    }
                    this.updateCurrentFieldTargets();
                    this.advanceState(elapsedSeconds);
                    this.animationTime += elapsedSeconds * this.motion;
                    if (!Number.isFinite(this.animationTime)) {
                        this.animationTime = 19.73;
                    } else if (this.animationTime >= 4096) {
                        this.animationTime %= 4096;
                    }
                }

                this.draw();

                if (
                    !this.reducedMotion
                    && (
                        !this.targetFrozen
                        || this.targetCharacter === 4
                        || this.stateIsMoving()
                    )
                ) {
                    this.requestFrame();
                }
            } catch (error) {
                this.fail(error);
            }
        }

        snapToTargets() {
            this.characterBlend.fill(0);
            this.characterBlend[this.targetCharacter] = 1;
            this.renderedEvolution = this.targetEvolution;
            this.renderedFocus = this.targetFocus;
            this.renderedCurrentFlow.set(this.targetCurrentFlow);
            this.renderedCurrentStrength = this.targetCurrentStrength;
            this.motion = this.targetFrozen ? 0 : 1;
            const accent = characterAccents[this.targetCharacter];
            this.renderedAccent[0] = accent[0];
            this.renderedAccent[1] = accent[1];
            this.renderedAccent[2] = accent[2];
        }

        advanceState(elapsedSeconds) {
            const characterAmount = smoothingAmount(elapsedSeconds, 0.55);
            let blendSum = 0;
            for (let index = 0; index < this.characterBlend.length; ++index) {
                const destination = index === this.targetCharacter ? 1 : 0;
                this.characterBlend[index] += (
                    destination - this.characterBlend[index]
                ) * characterAmount;
                blendSum += this.characterBlend[index];
            }

            if (blendSum > 0.0001) {
                for (
                    let index = 0;
                    index < this.characterBlend.length;
                    ++index
                ) {
                    this.characterBlend[index] /= blendSum;
                }
            }

            const controlAmount = smoothingAmount(elapsedSeconds, 0.18);
            this.renderedEvolution += (
                this.targetEvolution - this.renderedEvolution
            ) * controlAmount;
            this.renderedFocus += (
                this.targetFocus - this.renderedFocus
            ) * controlAmount;
            for (let axis = 0; axis < 2; ++axis) {
                this.renderedCurrentFlow[axis] += (
                    this.targetCurrentFlow[axis]
                    - this.renderedCurrentFlow[axis]
                ) * controlAmount;
            }
            this.renderedCurrentStrength += (
                this.targetCurrentStrength - this.renderedCurrentStrength
            ) * controlAmount;

            const targetAccent = characterAccents[this.targetCharacter];
            const colourAmount = smoothingAmount(elapsedSeconds, 0.42);
            for (let channel = 0; channel < 3; ++channel) {
                this.renderedAccent[channel] += (
                    targetAccent[channel] - this.renderedAccent[channel]
                ) * colourAmount;
            }

            const motionTime = this.targetFrozen ? 0.32 : 0.70;
            const motionAmount = smoothingAmount(elapsedSeconds, motionTime);
            this.motion += (
                (this.targetFrozen ? 0 : 1) - this.motion
            ) * motionAmount;
        }

        stateIsMoving() {
            if (Math.abs(this.motion - (this.targetFrozen ? 0 : 1)) > 0.001) {
                return true;
            }
            if (
                Math.abs(
                    this.renderedEvolution - this.targetEvolution
                ) > 0.001
                || Math.abs(this.renderedFocus - this.targetFocus) > 0.001
            ) {
                return true;
            }

            if (
                Math.abs(
                    this.renderedCurrentStrength
                    - this.targetCurrentStrength
                ) > 0.001
            ) {
                return true;
            }

            for (let axis = 0; axis < 2; ++axis) {
                if (
                    Math.abs(
                        this.renderedCurrentFlow[axis]
                        - this.targetCurrentFlow[axis]
                    ) > 0.001
                ) {
                    return true;
                }
            }

            for (
                let index = 0;
                index < this.characterBlend.length;
                ++index
            ) {
                const target = index === this.targetCharacter ? 1 : 0;
                if (Math.abs(this.characterBlend[index] - target) > 0.001) {
                    return true;
                }
            }

            return false;
        }

        draw() {
            const gl = this.gl;
            const { scene, horizontal, vertical } = this.targets;
            const character = this.characterBlend;
            for (let index = 0; index < 4; ++index) {
                this.legacyCharacterBlend[index] = character[index];
            }

            gl.disable(gl.DEPTH_TEST);
            gl.disable(gl.SCISSOR_TEST);
            gl.disable(gl.CULL_FACE);
            gl.disable(gl.BLEND);
            gl.bindVertexArray(this.vertexArray);

            gl.bindFramebuffer(gl.FRAMEBUFFER, scene.framebuffer);
            gl.viewport(0, 0, scene.width, scene.height);
            gl.useProgram(this.programs.scene);
            this.uniform2f(
                this.programs.scene,
                "uResolution",
                scene.width,
                scene.height
            );
            this.uniform1f(
                this.programs.scene,
                "uTime",
                this.animationTime
            );
            this.uniform1f(
                this.programs.scene,
                "uEvolution",
                this.renderedEvolution
            );
            this.uniform1f(
                this.programs.scene,
                "uFocus",
                this.renderedFocus
            );
            this.uniform1f(this.programs.scene, "uDirectOutput", 0);
            gl.uniform3fv(
                gl.getUniformLocation(this.programs.scene, "uAccent"),
                this.renderedAccent
            );
            gl.uniform4fv(
                gl.getUniformLocation(
                    this.programs.scene,
                    "uCharacterBlend"
                ),
                this.legacyCharacterBlend
            );
            this.uniform1f(
                this.programs.scene,
                "uCurrentBlend",
                character[4]
            );
            this.uniform2f(
                this.programs.scene,
                "uCurrentFlow",
                this.renderedCurrentFlow[0],
                this.renderedCurrentFlow[1]
            );
            this.uniform1f(
                this.programs.scene,
                "uCurrentStrength",
                this.renderedCurrentStrength
            );
            gl.drawArrays(gl.TRIANGLES, 0, 3);

            const characterRadius =
                character[0] * 1.00
                + character[1] * 1.25
                + character[2] * 0.86
                + character[3] * 1.35
                + character[4] * 1.18;
            const blurRadius =
                2.65
                * characterRadius
                * this.map(this.renderedEvolution, 0.88, 1.22)
                * this.map(this.renderedFocus, 1.16, 0.95);

            this.drawBlurPass(
                scene.texture,
                horizontal,
                blurRadius / scene.width,
                0
            );
            this.drawBlurPass(
                horizontal.texture,
                vertical,
                0,
                blurRadius * 0.5 / vertical.height
            );

            gl.bindFramebuffer(gl.FRAMEBUFFER, null);
            gl.viewport(0, 0, this.outputWidth, this.outputHeight);
            gl.useProgram(this.programs.composite);
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, scene.texture);
            gl.uniform1i(
                gl.getUniformLocation(this.programs.composite, "uScene"),
                0
            );
            gl.activeTexture(gl.TEXTURE1);
            gl.bindTexture(gl.TEXTURE_2D, vertical.texture);
            gl.uniform1i(
                gl.getUniformLocation(this.programs.composite, "uBloom"),
                1
            );
            this.uniform1f(
                this.programs.composite,
                "uBloomStrength",
                1
            );
            this.uniform1f(
                this.programs.composite,
                "uEvolution",
                this.renderedEvolution
            );
            this.uniform1f(
                this.programs.composite,
                "uFocus",
                this.renderedFocus
            );
            gl.uniform3fv(
                gl.getUniformLocation(this.programs.composite, "uAccent"),
                this.renderedAccent
            );
            gl.uniform4fv(
                gl.getUniformLocation(
                    this.programs.composite,
                    "uCharacterBlend"
                ),
                this.legacyCharacterBlend
            );
            this.uniform1f(
                this.programs.composite,
                "uCurrentBlend",
                character[4]
            );
            gl.drawArrays(gl.TRIANGLES, 0, 3);

            gl.activeTexture(gl.TEXTURE1);
            gl.bindTexture(gl.TEXTURE_2D, null);
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, null);
            gl.bindVertexArray(null);
            gl.useProgram(null);
        }

        drawBlurPass(sourceTexture, destination, directionX, directionY) {
            const gl = this.gl;
            gl.bindFramebuffer(gl.FRAMEBUFFER, destination.framebuffer);
            gl.viewport(0, 0, destination.width, destination.height);
            gl.useProgram(this.programs.blur);
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, sourceTexture);
            gl.uniform1i(
                gl.getUniformLocation(this.programs.blur, "uSource"),
                0
            );
            this.uniform2f(
                this.programs.blur,
                "uDirection",
                directionX,
                directionY
            );
            gl.drawArrays(gl.TRIANGLES, 0, 3);
        }

        uniform1f(program, name, value) {
            this.gl.uniform1f(
                this.gl.getUniformLocation(program, name),
                value
            );
        }

        uniform2f(program, name, x, y) {
            this.gl.uniform2f(
                this.gl.getUniformLocation(program, name),
                x,
                y
            );
        }

        updateCurrentFieldTargets() {
            const phaseA = Math.PI * 2
                * (0.13 + 0.0173 * this.currentFieldTime);
            const phaseB = Math.PI * 2
                * (0.61 + 0.0067 * this.currentFieldTime);
            const currentIsActive = this.targetCharacter === 4;

            this.targetCurrentFlow[0] = currentIsActive
                ? 0.78 * Math.cos(phaseA) + 0.22 * Math.cos(phaseB)
                : 0;
            this.targetCurrentFlow[1] = currentIsActive
                ? 0.78 * Math.sin(phaseA) - 0.22 * Math.sin(phaseB)
                : 0;

            const evolution = clampUnit(this.targetEvolution);
            const curvedEvolution =
                evolution * evolution * (3 - 2 * evolution);
            this.targetCurrentStrength = currentIsActive
                ? 0.06 + 0.94 * curvedEvolution
                : 0;
        }

        map(value, start, end) {
            return start + (end - start) * value;
        }

        requestFrame() {
            if (
                this.destroyed
                || this.failed
                || document.hidden
                || this.frameRequest
            ) {
                return;
            }
            this.frameRequest = window.requestAnimationFrame(this.onFrame);
        }

        cancelFrame() {
            if (this.frameRequest) {
                window.cancelAnimationFrame(this.frameRequest);
                this.frameRequest = 0;
            }
        }

        deleteTargets() {
            if (!this.gl || !this.targets) {
                this.targets = null;
                return;
            }

            for (const target of Object.values(this.targets)) {
                this.gl.deleteFramebuffer(target.framebuffer);
                this.gl.deleteTexture(target.texture);
            }
            this.targets = null;
        }

        deleteResources() {
            if (!this.gl) {
                return;
            }

            this.deleteTargets();
            if (this.programs) {
                for (const program of Object.values(this.programs)) {
                    this.gl.deleteProgram(program);
                }
                this.programs = null;
            }
            if (this.vertexArray) {
                this.gl.deleteVertexArray(this.vertexArray);
                this.vertexArray = null;
            }
        }

        fail(error) {
            this.failed = true;
            this.cancelFrame();
            root.classList.remove("ocean-webgl-ready");
            root.classList.add("ocean-webgl-fallback");
            this.canvas?.classList.add("is-fallback");
            if (this.canvas) {
                this.canvas.hidden = true;
            }

            if (!this.failureReported) {
                this.failureReported = true;
                console.error(
                    "Amanita Ocean background could not start; "
                    + "using the CSS fallback.",
                    error
                );
            }
        }
    }

    window.OceanBackground = OceanBackground;
})();
