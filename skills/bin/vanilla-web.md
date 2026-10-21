# Vanilla Web (No Dependencies)

Use this when building a page or UI in plain HTML/CSS/JS that needs to look genuinely good —
not a framework starter-template look — with zero external requests: no CDN frameworks, no
web fonts, no icon libraries, no build step. Everything ships in the files themselves.

## Ground rule: no external dependencies means none

- No `<link>` to Google Fonts or any other remote font host. No CDN script tags (no Bootstrap,
  no Tailwind CDN build, no jQuery, no icon-font CDN). No remote images unless the project
  supplies its own. If it needs a network request at page-load to render correctly, it's a
  dependency — cut it or inline it.
- This isn't just an offline-friendliness rule — it also forces design decisions to come from
  CSS itself rather than a framework's opinion, which is where a distinctive look comes from.

## Where "looks amazing" actually comes from without a framework

- **System font stack, used deliberately** — you don't need a webfont to get good typography.
  A stack like `font-family: -apple-system, "Segoe UI", "Inter", system-ui, sans-serif` combined
  with a real type scale (not just default sizes) does most of the work. Set the scale with a
  ratio (1.25 or 1.333 works well) rather than picking sizes ad hoc, and give headings tighter
  `letter-spacing` and `line-height` than body text — that contrast alone reads as intentional.
- **A real color system, not browser defaults** — define 4-6 CSS custom properties for the
  palette (`--bg`, `--surface`, `--text`, `--text-muted`, `--accent`, `--accent-2`) at `:root`
  and use only those, never a one-off hex buried in a rule. This is also what makes a dark-mode
  toggle nearly free later: swap the custom property values, nothing else changes.
- **Depth from CSS, not images** — `box-shadow` (layer 2-3 shadows at different blur/opacity
  for a soft realistic effect, not one harsh default shadow), `backdrop-filter: blur()` for
  glass effects, CSS `gradient` backgrones (`linear-gradient`, `radial-gradient`, `conic-gradient`)
  for texture that would otherwise need a background image.
- **Shape from `clip-path` and `border-radius`** — asymmetric radii (`border-radius: 24px 8px
  24px 8px`), `clip-path: polygon(...)` for non-rectangular sections, `mask-image` with a
  gradient for fade edges. These read as custom-designed, not templated, and cost nothing.
- **Motion from native CSS, not a library** — `@keyframes` for load-in sequences,
  `transition` on `transform`/`opacity` for hover and state changes, the
  `animation-timeline: view()` / `@scroll-timeline` (or an `IntersectionObserver` fallback for
  broader support) for scroll-triggered reveals. Respect `prefers-reduced-motion` — wrap
  nontrivial animation in `@media (prefers-reduced-motion: no-preference)`.
- **Icons as inline SVG, not a font** — hand-write or inline small SVGs directly in the HTML
  (or as CSS `background-image: url("data:image/svg+xml,...")` for decorative ones). This
  avoids the icon-font CDN dependency entirely and renders sharper.
- **Grain/texture without an image file** — an SVG `<feTurbulence>` filter or a repeating
  `linear-gradient` at 1-2px intervals can fake noise/texture entirely in CSS if the design
  calls for it, no downloaded asset needed.

## Structure and layout

- CSS Grid and Flexbox cover essentially everything a framework's layout utilities would have
  done — `grid-template-columns: repeat(auto-fit, minmax(...))` replaces most "responsive grid"
  needs without a single media query.
- Use semantic HTML elements (`<nav>`, `<main>`, `<article>`, `<section>`, `<figure>`) — this
  is free accessibility and free structure, and it's what CSS's own cascade and selectors are
  built to target well.
- Container queries (`@container`) where a component needs to respond to its own container's
  size rather than the viewport — increasingly well supported and removes a whole class of
  "works in the sidebar but not the main column" bugs that media queries can't solve.

## JavaScript, if any

- Vanilla DOM APIs (`querySelector`, `addEventListener`, `classList`, `dataset`) are enough for
  the vast majority of interactivity — toggles, tabs, accordions, form validation, scroll
  effects. Reach for a library only if genuinely justified, and then it's no longer a
  zero-dependency page.
- `<template>` + `cloneNode` for repeated markup instead of string-concatenating HTML.
- Keep JS additive: the page should still make sense with JS disabled/failed where feasible
  (progressive enhancement), especially for content and navigation.

## Process

1. Before writing markup, decide the token system: palette (as hex values), type scale (base
   size + ratio), and spacing scale (a consistent step, e.g. 4/8/12/16/24/32/48/64px) — put all
   three in `:root` custom properties before anything else.
2. Build the structure in semantic HTML first, unstyled, and confirm it makes sense read
   top-to-bottom with no CSS.
3. Layer in layout (Grid/Flexbox), then color/type from the token system, then the one or two
   signature visual details (a gradient, a shape, a motion sequence) — don't scatter effort
   evenly across many small flourishes.
4. Check it at mobile width and with keyboard-only navigation before considering it done.

## What NOT to do

- Don't reach for a CDN "just this once" for a font or icon set — it silently breaks the
  no-dependency constraint and the offline/build-free guarantee that was the point.
- Don't default to the generic AI-page look: cream background with a serif display and a
  terracotta accent, or a near-black background with one neon accent — these are recognizable
  defaults, not designed choices. Ground the palette and type in the actual subject instead.
- Don't animate everything — pick the one moment (page load, a hover state, a scroll reveal)
  that deserves motion and leave the rest still; scattered animation reads as templated, not
  polished.
- Don't skip focus states — removing the default `outline` without providing a visible
  replacement breaks keyboard navigation entirely, not just aesthetically.
