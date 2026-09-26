# Touchpad integration patch

Upstream: WerWolv/libtesla, commit `f766e9b607a05e9756843cbd62b3bfb98be1646c`.

Changes to `include/tesla.hpp`:

1. Add public virtual `Overlay::onHidden()` with an empty default implementation.
2. Invoke it immediately after the main loop calls `hlp::requestForeground(false)`, after the clear/fade phase.

Touchpad overrides this hook to commit a prepared enable/pair request only after gameplay/system input has been returned. Existing `onHide()` remains at animation start and is not used for committing input.

The modification is local to this project's statically compiled overlay. No Tesla Menu or nx-ovlloader binary is replaced. All original license and copyright notices are retained.

## rc7 / rc8 additions

The current integration also fixes input mutex scope, hide request thread ownership, thread/event lifetime, empty GUI stack handling and foreground error reporting. rc8 removes inactive menu layers from VI stacks and shows the menu after a confirmed backend Pause. It uses common/layer_stacks.hpp, adds canShow/onLayerChange/onHideCause callbacks, records hide-source bits, and preserves pending asynchronous IPC pages through canClose. See docs/RUNTIME_RC7.md and docs/RUNTIME_RC8.md in the matching source archive.
