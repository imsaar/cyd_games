#pragma once

void wifi_init();
bool wifi_connected();
void wifi_loop();  // Handle reconnection
void wifi_disable();  // Disconnect and stop reconnection (not persisted)
bool wifi_disabled();
void wifi_enable();   // Re-enable and reconnect
