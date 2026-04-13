#pragma once

void ota_init();
void ota_loop();
void ota_validate_app();  // Mark current partition as valid (call early in setup)
