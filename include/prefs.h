#pragma once

#define MAX_BRIGHTNESS 4
#define DEFAULT_BRIGHTNESS 2
#define DEFAULT_M12_MODE false
#define DEFAULT_OFFSET 7 // -360 minutes
#define DEFAULT_FLIPPED false

void loadPreferences();

void savePrefs(const char *name, bool m12, uint8_t uo, uint8_t db, bool fd);
