#include "android_storage.h"

#ifdef HELTEC_ANDROID_STORAGE

#include "sd_functions.h"
#include <globals.h>
#include <mbedtls/base64.h>
#include <vector>

namespace {
constexpr size_t kMaxArgs = 8;
constexpr size_t kReadChunk = 384;
struct ActivePath {
    String path;
    size_t references;
};

StaticSemaphore_t activePathsMutexBuffer;
SemaphoreHandle_t activePathsMutex = nullptr;
portMUX_TYPE activePathsInitMux = portMUX_INITIALIZER_UNLOCKED;
std::vector<ActivePath> activePaths;

bool lockActivePaths() {
    if (!activePathsMutex) {
        portENTER_CRITICAL(&activePathsInitMux);
        if (!activePathsMutex) activePathsMutex = xSemaphoreCreateMutexStatic(&activePathsMutexBuffer);
        portEXIT_CRITICAL(&activePathsInitMux);
    }
    return activePathsMutex && xSemaphoreTake(activePathsMutex, portMAX_DELAY) == pdTRUE;
}

void unlockActivePaths() { xSemaphoreGive(activePathsMutex); }

uint32_t updateCrc32(uint32_t crc, const uint8_t *data, size_t length) {
    for (size_t index = 0; index < length; index++) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

String crc32Text(uint32_t value) {
    char output[9];
    snprintf(output, sizeof(output), "%08lX", static_cast<unsigned long>(value));
    return String(output);
}

bool parseCrc32(const String &value, uint32_t &output) {
    if (value.length() != 8) return false;
    output = 0;
    for (size_t index = 0; index < value.length(); index++) {
        const char c = value[index];
        const int nibble = c >= '0' && c <= '9' ? c - '0'
                           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                  : -1;
        if (nibble < 0) return false;
        output = (output << 4) | static_cast<uint32_t>(nibble);
    }
    return true;
}

bool storageCrc32(const String &path, size_t &size, uint32_t &output) {
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        return false;
    }
    size = file.size();
    uint8_t buffer[1024];
    uint32_t crc = 0xFFFFFFFFu;
    while (file.available()) {
        const size_t received = file.read(buffer, sizeof(buffer));
        if (received == 0) {
            file.close();
            return false;
        }
        crc = updateCrc32(crc, buffer, received);
        delay(0);
    }
    file.close();
    output = ~crc;
    return true;
}

enum class RemoveInactiveResult { Removed, Active, Failed };

RemoveInactiveResult removeInactiveFile(const String &path) {
    if (!lockActivePaths()) return RemoveInactiveResult::Active;
    for (const ActivePath &active : activePaths) {
        if (active.path == path) {
            unlockActivePaths();
            return RemoveInactiveResult::Active;
        }
    }
    const bool removed = SD.remove(path);
    unlockActivePaths();
    return removed ? RemoveInactiveResult::Removed : RemoveInactiveResult::Failed;
}

size_t splitCommand(const String &line, String (&args)[kMaxArgs]) {
    size_t count = 0;
    String current;
    char quote = 0;
    bool escaped = false;
    for (size_t i = 0; i < line.length(); ++i) {
        const char c = line[i];
        if (escaped) {
            current += c;
            escaped = false;
        } else if (c == '\\' && quote != 0) {
            escaped = true;
        } else if (quote != 0) {
            if (c == quote) quote = 0;
            else current += c;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (isSpace(c)) {
            if (current.length() > 0 && count < kMaxArgs) {
                args[count++] = current;
                current = "";
            }
        } else {
            current += c;
        }
    }
    if (current.length() > 0 && count < kMaxArgs) args[count++] = current;
    return count;
}

bool safePath(String &path) {
    path.trim();
    if (path.length() == 0 || path.length() > 240) return false;
    if (!path.startsWith("/")) path = "/" + path;
    size_t start = 1;
    while (start <= path.length()) {
        int end = path.indexOf('/', start);
        if (end < 0) end = path.length();
        if (path.substring(start, end) == "..") return false;
        if (static_cast<size_t>(end) >= path.length()) break;
        start = end + 1;
    }
    for (size_t i = 0; i < path.length(); ++i) {
        if (static_cast<uint8_t>(path[i]) < 0x20) return false;
    }
    return true;
}

bool unsignedNumber(const String &value, size_t &output) {
    if (value.length() == 0) return false;
    for (size_t i = 0; i < value.length(); ++i) {
        if (!isDigit(value[i])) return false;
    }
    output = static_cast<size_t>(strtoull(value.c_str(), nullptr, 10));
    return true;
}

void storageError(const String &message) { serialDevice->println("SD:ERR:" + message); }

bool ensureStorage() {
    if (setupSdCard()) return true;
    storageError("not_mounted");
    return false;
}

void listStorage(const String &requested) {
    String path = requested.length() == 0 ? "/" : requested;
    if (!safePath(path)) {
        storageError("invalid_path");
        return;
    }
    File directory = SD.open(path);
    if (!directory || !directory.isDirectory()) {
        if (directory) directory.close();
        storageError("cannot_open:" + path);
        return;
    }

    serialDevice->println("SD:LIST:" + path);
    size_t count = 0;
    while (true) {
        bool isDirectory = false;
        String fullPath = directory.getNextFileName(&isDirectory);
        if (fullPath.length() == 0) break;
        String name = fullPath.substring(fullPath.lastIndexOf('/') + 1);
        if (isDirectory) {
            serialDevice->println("SD:DIR:[" + String(count) + "] " + name);
        } else {
            File file = SD.open(fullPath, FILE_READ);
            const size_t size = file ? file.size() : 0;
            const time_t modified = file ? file.getLastWrite() : 0;
            if (file) file.close();
            serialDevice->println(
                "SD:FILE:[" + String(count) + "] " + name + " " + String(size) + " " +
                String(static_cast<uint32_t>(modified))
            );
        }
        ++count;
    }
    directory.close();
    if (count == 0) serialDevice->println("SD:EMPTY");
    serialDevice->println("SD:OK:listed " + String(count) + " entries");
}

void readStorage(const String &requested, const String &offsetValue, const String &lengthValue) {
    String path = requested;
    size_t offset = 0;
    size_t requestedLength = 0;
    if (!safePath(path) || !unsignedNumber(offsetValue, offset) ||
        !unsignedNumber(lengthValue, requestedLength)) {
        storageError("invalid_read_request");
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        storageError("cannot_open:" + path);
        return;
    }
    const size_t fileSize = file.size();
    if (offset > fileSize) offset = fileSize;
    const size_t length = min(requestedLength, fileSize - offset);
    if (!file.seek(offset)) {
        file.close();
        storageError("seek_failed");
        return;
    }

    serialDevice->println("SD:READ:BEGIN:" + path);
    serialDevice->println("SD:READ:SIZE:" + String(fileSize));
    serialDevice->println("SD:READ:OFFSET:" + String(offset));
    serialDevice->println("SD:READ:LENGTH:" + String(length));
    serialDevice->println("SD:READ:ENCODING:base64");

    uint8_t input[kReadChunk];
    unsigned char encoded[((kReadChunk + 2) / 3) * 4 + 1];
    size_t total = 0;
    while (total < length) {
        const size_t wanted = min(kReadChunk, length - total);
        const size_t received = file.read(input, wanted);
        if (received == 0) break;
        size_t encodedLength = 0;
        if (mbedtls_base64_encode(encoded, sizeof(encoded), &encodedLength, input, received) != 0) {
            file.close();
            storageError("base64_encode_failed");
            return;
        }
        encoded[encodedLength] = '\0';
        serialDevice->println("SD:READ:DATA:" + String(reinterpret_cast<char *>(encoded)));
        total += received;
    }
    file.close();
    serialDevice->println("SD:READ:END:bytes=" + String(total));
    if (total == length) serialDevice->println("SD:OK");
    else storageError("file_read_failed");
}

void writeStorage(const String &operation, const String &requested, const String &encoded) {
    String path = requested;
    if (!safePath(path) || path == "/") {
        storageError("invalid_path");
        return;
    }
    const size_t capacity = (encoded.length() * 3) / 4 + 4;
    uint8_t *decoded = static_cast<uint8_t *>(malloc(capacity));
    if (!decoded) {
        storageError("oom");
        return;
    }
    size_t decodedLength = 0;
    const int result = mbedtls_base64_decode(
        decoded,
        capacity,
        &decodedLength,
        reinterpret_cast<const unsigned char *>(encoded.c_str()),
        encoded.length()
    );
    if (result != 0) {
        free(decoded);
        storageError("base64_decode_failed");
        return;
    }

    const char *mode = operation == "append" ? FILE_APPEND : FILE_WRITE;
    File file = SD.open(path, mode, true);
    if (!file) {
        free(decoded);
        storageError((operation == "append" ? "cannot_open:" : "cannot_create:") + path);
        return;
    }
    const size_t written = file.write(decoded, decodedLength);
    file.close();
    free(decoded);
    serialDevice->println(
        String(operation == "append" ? "SD:APPEND:bytes=" : "SD:WRITE:bytes=") + written
    );
    if (written != decodedLength) {
        storageError("short_write:" + path);
    } else {
        serialDevice->println(
            String(operation == "append" ? "SD:OK:appended:" : "SD:OK:created:") + path
        );
    }
}
} // namespace

void androidStorageMarkActive(const String &path) {
    if (!lockActivePaths()) return;
    for (ActivePath &active : activePaths) {
        if (active.path == path) {
            active.references++;
            unlockActivePaths();
            return;
        }
    }
    activePaths.push_back({path, 1});
    unlockActivePaths();
}

void androidStorageMarkClosed(const String &path) {
    if (!lockActivePaths()) return;
    for (auto item = activePaths.begin(); item != activePaths.end(); ++item) {
        if (item->path == path) {
            if (item->references > 1) item->references--;
            else activePaths.erase(item);
            break;
        }
    }
    unlockActivePaths();
}

bool androidStoragePathIsActive(const String &path) {
    if (!lockActivePaths()) return true;
    bool found = false;
    for (const ActivePath &active : activePaths) {
        if (active.path == path) {
            found = true;
            break;
        }
    }
    unlockActivePaths();
    return found;
}

AndroidStorageActiveGuard::AndroidStorageActiveGuard(const String &path, bool enabled)
    : trackedPath(path), tracking(enabled && !path.isEmpty()) {
    if (tracking) androidStorageMarkActive(trackedPath);
}

AndroidStorageActiveGuard::~AndroidStorageActiveGuard() {
    if (tracking) androidStorageMarkClosed(trackedPath);
}

bool handleAndroidStorageCommand(const String &line) {
    String args[kMaxArgs];
    const size_t count = splitCommand(line, args);
    if (count == 0 || args[0] != "sd") return false;
    if (count < 2) {
        storageError("usage");
        return true;
    }
    if (!ensureStorage()) return true;

    const String &operation = args[1];
    if (operation == "status") {
        serialDevice->println("SD:STATUS:mounted=true");
        serialDevice->println("SD:STATUS:type=virtual");
        serialDevice->println("SD:STATUS:total=" + String(SD.totalBytes()));
        serialDevice->println("SD:STATUS:free=" + String(SD.freeBytes()));
        serialDevice->println("SD:OK");
    } else if (operation == "list") {
        listStorage(count >= 3 ? args[2] : "/");
    } else if (operation == "size" && count >= 3) {
        String path = args[2];
        if (!safePath(path)) {
            storageError("invalid_path");
        } else {
            File file = SD.open(path, FILE_READ);
            if (!file || file.isDirectory()) {
                if (file) file.close();
                storageError("not_found:" + path);
            } else {
                serialDevice->println("SD:SIZE:" + String(file.size()));
                file.close();
                serialDevice->println("SD:OK");
            }
        }
    } else if (operation == "crc32" && count >= 3) {
        String path = args[2];
        size_t size = 0;
        uint32_t crc32 = 0;
        if (!safePath(path)) storageError("invalid_path");
        else if (androidStoragePathIsActive(path)) storageError("active_file:" + path);
        else if (!storageCrc32(path, size, crc32)) storageError("not_found:" + path);
        else if (androidStoragePathIsActive(path)) storageError("active_file:" + path);
        else {
            serialDevice->println("SD:CRC32:" + crc32Text(crc32));
            serialDevice->println("SD:CRC32:SIZE:" + String(size));
            serialDevice->println("SD:OK");
        }
    } else if (operation == "ack" && count >= 5) {
        String path = args[2];
        size_t expectedSize = 0;
        uint32_t expectedCrc32 = 0;
        size_t actualSize = 0;
        uint32_t actualCrc32 = 0;
        if (
            !safePath(path) || path == "/" || !unsignedNumber(args[3], expectedSize) ||
            !parseCrc32(args[4], expectedCrc32)
        ) {
            storageError("invalid_ack");
        } else if (androidStoragePathIsActive(path)) {
            storageError("active_file:" + path);
        } else if (!storageCrc32(path, actualSize, actualCrc32)) {
            storageError("not_found:" + path);
        } else if (actualSize != expectedSize) {
            storageError("ack_size_changed:" + path);
        } else if (actualCrc32 != expectedCrc32) {
            storageError("ack_checksum_changed:" + path);
        } else {
            const RemoveInactiveResult removed = removeInactiveFile(path);
            if (removed == RemoveInactiveResult::Active) {
                storageError("active_file:" + path);
            } else if (removed == RemoveInactiveResult::Failed) {
                storageError("ack_release_failed:" + path);
            } else {
                serialDevice->println("SD:ACK:released=" + path);
                serialDevice->println("SD:ACK:size=" + String(actualSize));
                serialDevice->println("SD:ACK:crc32=" + crc32Text(actualCrc32));
                serialDevice->println("SD:OK:released:" + path);
            }
        }
    } else if (operation == "read" && count >= 5) {
        readStorage(args[2], args[3], args[4]);
    } else if ((operation == "write" || operation == "append") && count >= 4) {
        writeStorage(operation, args[2], args[3]);
    } else if (operation == "mkdir" && count >= 3) {
        String path = args[2];
        if (!safePath(path) || path == "/") storageError("invalid_path");
        else if (SD.exists(path) || SD.mkdir(path)) serialDevice->println("SD:OK:mkdir:" + path);
        else storageError("mkdir_failed:" + path);
    } else if (operation == "rm" && count >= 3) {
        String path = args[2];
        if (!safePath(path) || path == "/") {
            storageError("invalid_path");
        } else if (androidStoragePathIsActive(path)) {
            storageError("active_file:" + path);
        } else {
            File target = SD.open(path);
            if (!target) storageError("not_found:" + path);
            else {
                const bool directory = target.isDirectory();
                target.close();
                const bool removed = directory ? SD.rmdir(path) : SD.remove(path);
                if (removed) serialDevice->println("SD:OK:removed:" + path);
                else storageError("remove_failed:" + path);
            }
        }
    } else {
        storageError("unsupported");
    }
    return true;
}

#else

bool handleAndroidStorageCommand(const String &) { return false; }
void androidStorageMarkActive(const String &) {}
void androidStorageMarkClosed(const String &) {}
bool androidStoragePathIsActive(const String &) { return false; }
AndroidStorageActiveGuard::AndroidStorageActiveGuard(const String &path, bool enabled)
    : trackedPath(path), tracking(enabled && !path.isEmpty()) {}
AndroidStorageActiveGuard::~AndroidStorageActiveGuard() {}

#endif
