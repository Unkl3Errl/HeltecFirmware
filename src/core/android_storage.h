#pragma once

#include <Arduino.h>

// Handles the cross-firmware `sd` command protocol used by HeltecController.
// Returns false when the line is not a storage command.
bool handleAndroidStorageCommand(const String &line);

// Writers register logical live files so checksum/ack cannot race an open or
// still-growing capture. Calls are no-ops on boards without Android storage.
void androidStorageMarkActive(const String &path);
void androidStorageMarkClosed(const String &path);
bool androidStoragePathIsActive(const String &path);

class AndroidStorageActiveGuard {
public:
    AndroidStorageActiveGuard(const String &path, bool enabled = true);
    ~AndroidStorageActiveGuard();

    AndroidStorageActiveGuard(const AndroidStorageActiveGuard &) = delete;
    AndroidStorageActiveGuard &operator=(const AndroidStorageActiveGuard &) = delete;

private:
    String trackedPath;
    bool tracking = false;
};
