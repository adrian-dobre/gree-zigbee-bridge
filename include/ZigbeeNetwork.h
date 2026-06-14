#pragma once

namespace zigree {

// ---------------------------------------------------------------------------
// Helpers for managing the zboss network-state partitions.
//
// Clearing the stored network state forces a clean rejoin. This must happen
// BEFORE Zigbee.begin() and, unlike Zigbee.factoryReset(), does not require the
// stack to be running (the latter asserts if called pre-init). Stale state left
// by an earlier firmware revision with a different cluster layout otherwise
// causes the stack to crash on rejoin.
// ---------------------------------------------------------------------------

// Clear stored Zigbee state once per firmware image, or whenever the BOOT
// button is held at startup. A guard flag in NVS survives clean reboots so the
// device does not loop re-pairing.
void maybeResetZigbeeNetwork();

}  // namespace zigree
