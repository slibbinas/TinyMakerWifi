# TinyMakerWifi — Brand & Design System

Internal design reference for the TinyMakerWifi web surfaces (landing page, user
manual, dashboard, social cards). It **documents the tokens already in use** so new
pages stay visually consistent and avoid a generic "AI-sloppy" look.

- Source of truth for these tokens: `docs/landing/index.html` (`:root` / `[data-theme=light]`),
  `web/dashboard.html` (`:root`), `docs/og-card.src.html`.
- Scope: web/marketing/docs only. The on-device firmware UI (Arduino_GFX screens) is
  constrained hardware rendering and is not governed here.
- Status: internal (git). Publish as a public page later if useful. Not on the firmware
  version ladder — changing this file has **no effect on releases** and never ships in
  `firmware.bin`.

---

## 1. Brand color

| Role | Value | Notes |
|---|---|---|
| **Accent / brand orange** | `#e8720c` | Primary buttons, logo, step numbers, highlights |
| Accent hover | `#c95f06` | Darker orange on hover for primary buttons |
| Logo arc (blue) | `#4da3ff` | The "WiFi / rising" arc over the logo bars |

The orange is the single brand signal. Use it for the one primary action per view; do not
paint large areas with it.

## 2. Logo

An SVG mark: three stacked orange bars (opacity `1` / `.75` / `.5`, largest at the bottom)
with a blue arc (`#4da3ff`) rising over them — reads as a printer/stack plus a WiFi signal.
Wordmark: **TinyMaker** in brand orange, "Wifi" in white/light. Font is the system stack
(below). Keep the arc blue — it is the only cool accent and carries the "wireless" meaning.

## 3. Palette (dark default → light)

Dark is the default theme; light is applied via `[data-theme=light]`.

| Token | Dark | Light |
|---|---|---|
| Background | `#141416` | `#f2f2f4` |
| Card / surface | `#1d1d20` | `#ffffff` |
| Line / border | `#26262a` / `#2c2c31` | `#dfe1e5` / `#d9dbe0` |
| Text | `#eeeeee` | `#1f2124` |
| Muted text | `#aaaaaa` / `#8a8a92` | `#5f6570` / `#6a707a` |
| Link | `#84bcf8` | `#155fb0` |
| Secondary button | `#2e2e33` (hover `#3c3c42`) | `#e2e4e8` (hover `#d3d6db`) |

**Status colors:** success `#2fbf4f`, warning `#ffb15f`, danger `#7b2f2f`.

> **Known variance to reconcile:** the dashboard uses a slightly different dark background
> (`#1c1c1e`, card `#2a2a2e`) than the landing/manual/og-card (`#141416`, card `#1d1d20`).
> Prefer `#141416` as the canonical brand background; align the dashboard when convenient.

## 4. Typography

- **Family:** `-apple-system, "Segoe UI", Roboto, sans-serif` (system stack — no web fonts,
  keeps pages self-contained and fast).
- **Code / monospace:** `monospace`.
- Body line-height ~`1.55`. Weights: `600` for buttons/emphasis, `700` for step badges.

## 5. Components

- **Primary button:** accent background `#e8720c`, white text, weight `600`,
  `border-radius: 10px`, hover `#c95f06`.
- **Secondary button:** `--btnsec` background, standard text color, same radius.
- **Badges / inputs:** `border-radius` ~`6–9px`.
- **Pills / avatars / step circles:** `border-radius: 50%`.
- **Theme-color meta:** `#141416` (dark) / `#f2f2f4` (light) — keep in sync with the palette.

## 6. Theming rules

- Ship **both** themes. Dark is default (`color-scheme: dark`); light via
  `:root[data-theme=light]` / `[data-theme=light]`.
- Drive colors through CSS custom properties (`--bg`, `--card`, `--text`, `--accent`, …),
  never hard-coded hex in markup, so the theme toggle works everywhere.
- Keep pages **self-contained**: inline CSS, no external fonts/CDNs.

## 7. Voice & tone

- Plain, friendly, respectful. Explain the human benefit, not just the mechanism.
- Lead with "free and open source"; credit **TinyMaker3D** for the original firmware/hardware.
- Support ask stays light: "Buy me a coffee ☕" (PayPal), never pushy.

---

*Related: Front-End Design skill trial (issue #61). When building or restyling a web page,
read this first and reuse these tokens rather than inventing new ones.*
