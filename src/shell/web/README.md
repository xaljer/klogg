# klogg web shell

Svelte 5 + TypeScript + xterm.js front end for klogg. It talks to `klogg_server` over
HTTP/SSE; the Electron shell and the server both load the built UI from `out/web`.

Build, test, and packaging commands live in the repository `BUILD.md`.

## Layout

| Path | Contents |
|---|---|
| `src/lib` | API client, state modules, selection/projection logic (unit-tested) |
| `src/components` | Svelte components: pane, main viewport, filtered view, toolbar, dialogs |
| `src/app.css` | Layout and view styling |
| `tests` | Node unit tests for pure logic and i18n |
| `e2e` | Playwright specs against a real server and browser |
| `spike/xterm-logview` | Standalone xterm feasibility spike, not part of the shipped app |

## References

- `BUILD.md` — build, test, type check, e2e, and Electron packaging.
- `docs/architecture/web-api.md` — HTTP/SSE contract.
- `docs/architecture/web-ui-parity.md` — desktop parity list.
- `docs/architecture/plans/2026-11-28-web-ui-capability-ledger.md` — capability status and
  UI invariants.
