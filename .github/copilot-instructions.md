# Copilot Instructions

## Build, test, and lint

- Configure and build the core logic without SDL: `cmake -S . -B build/test-core -DSPIDER_BUILD_APP=OFF && cmake --build build/test-core`
- Run the current test suite: `ctest --test-dir build/test-core --output-on-failure`
- Run the single existing test target directly: `./build/test-core/spider_core_tests`
- Run the app smoke test directly: `./build/test-core/spider_app_smoke_test`
- Configure the full app build once SDL dependencies are installed: `cmake -S . -B build/app-default`
- On this machine, prefer `rpm-ostree install SDL3-devel SDL3_image-devel` over `dnf`; `dnf` is blocked here.

The real SDL app target requires both `SDL3` and `SDL3_image` CMake packages.

## High-level architecture

The project is split into a framework-light game core and an optional SDL3 front end.

- `src/core/` contains the game-state and responsive-layout logic. This layer owns shuffling, the 10-stack tableau deal, stock dealing, move validation, same-suit sequence rules, completed-run removal, and playfield sizing.
- `tests/game_state_test.cpp` exercises the core mechanics without SDL, and `spider_app_smoke_test` uses fake SDL headers to validate that the app translation unit still builds and exits cleanly in headless environments.
- `src/app/main.cpp` is the SDL3 shell. It creates the resizable window, handles hidden-scroll playfield navigation, draws a minimal in-game control bar, supports both click-to-move and drag-and-drop, and renders cards from the sprite sheet.
- `src/assets/game-window.png` is the layout reference for the table-first UI.
- `src/assets/cards.png` is the atlas for card faces and backs.

The core/app split is intentional: mechanics should stay testable without SDL, while rendering and input should stay isolated in the app layer.

## Key conventions

- The game is a two-suit Spider variant using only hearts and spades, with stock dealing allowed even when some tableau columns are empty.
- The first milestone is intentionally table-first: keep UI scope centered on the playfield and essential in-game controls, not menu screens.
- Tableau layout must always preserve 10 live stacks with 9 adaptive gaps; stack width and spacing scale with the window width instead of using fixed pixel values.
- Vertical overflow is handled by scrolling the playfield content, but no visible scrollbars should be introduced.
- `cards.png` is treated as a 13-column atlas with explicit x/y boundaries in `src/app/main.cpp`; card-back rendering uses the blue-back cell from the last row.
- Keep gameplay rules in `src/core/` and keep SDL-specific rendering/input concerns in `src/app/`.
