import hashlib
from typing import TYPE_CHECKING, Any
import requests

if TYPE_CHECKING:
    Import: Any = None
    env: Any = {}

import glob
import gzip
import subprocess
from os import makedirs, remove, replace
from os.path import basename, dirname, exists, isfile, join
from shutil import copy2

Import("env")  # type: ignore

FRAMEWORK_DIR = env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs")
board_mcu = env.BoardConfig()
mcu = board_mcu.get("build.mcu", "")
library_dir = join(FRAMEWORK_DIR, mcu, "lib")
patchflag_path = join(library_dir, ".patched")


def _tool_path(package_names, executable):
    for package_name in package_names:
        package_dir = env.PioPlatform().get_package_dir(package_name)
        if package_dir:
            candidate = join(package_dir, "bin", executable)
            if isfile(candidate):
                return candidate
    raise RuntimeError(f"Patch: {executable} was not found in the PlatformIO toolchain")


def _touch(path):
    with open(path, "w") as fp:
        fp.write("")


def patch_net80211():
    if mcu == "esp32p4":
        return

    original_file = join(library_dir, "libnet80211.a")
    backup_file = f"{original_file}.old"
    patched_file = f"{original_file}.patched"

    # Recover from an interrupted/failed legacy patch before deciding whether
    # the framework archive is already ready for linking.
    recovered_legacy_failure = False
    if not isfile(original_file) and isfile(backup_file):
        copy2(backup_file, original_file)
        recovered_legacy_failure = True
    if isfile(patchflag_path) and isfile(original_file) and not recovered_legacy_failure:
        return
    if not isfile(original_file):
        raise RuntimeError(f"Patch: original archive not found: {original_file}")

    if mcu in ("esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32h2"):
        objcopy = _tool_path(("toolchain-riscv32-esp",), "riscv32-esp-elf-objcopy")
    else:
        objcopy = _tool_path(
            ("toolchain-xtensa-esp-elf", f"toolchain-xtensa-{mcu}"),
            f"xtensa-{mcu}-elf-objcopy",
        )

    if isfile(patched_file):
        remove(patched_file)
    subprocess.run(
        [
            objcopy,
            "--weaken-symbol=ieee80211_raw_frame_sanity_check",
            original_file,
            patched_file,
        ],
        check=True,
    )
    if not isfile(patched_file):
        raise RuntimeError("Patch: objcopy did not create the patched archive")

    # Keep a pristine recovery copy and replace the link input only after the
    # patched output has been produced successfully.
    copy2(original_file, backup_file)
    replace(patched_file, original_file)
    _touch(patchflag_path)


patch_net80211()


def hash_file(file_path):
    """Generate SHA-256 hash for a single file."""
    hasher = hashlib.sha256()
    with open(file_path, "rb") as f:
        # Read the file in chunks to avoid memory issues
        for chunk in iter(lambda: f.read(4096), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def hash_files(file_paths):
    """Generate a combined hash for multiple files."""
    combined_hash = hashlib.sha256()

    for file_path in file_paths:
        file_hash = hash_file(file_path)
        combined_hash.update(file_hash.encode("utf-8"))  # Update with the file's hash

    return combined_hash.hexdigest()


def save_checksum_file(hash_value, output_file):
    """Save the hash value to a specified output file."""
    with open(output_file, "w") as f:
        f.write(hash_value)


def load_checksum_file(input_file):
    """Load the hash value from a specified input file."""
    with open(input_file, "r") as f:
        return f.readline().strip()


def minify_css(c):
    minify_req = requests.post(
        "https://www.toptal.com/developers/cssminifier/api/raw",
        {"input": c.read().decode('utf-8')},
    )
    return c if minify_req is False else minify_req.text.encode('utf-8')


def minify_js(js):
    minify_req = requests.post(
        'https://www.toptal.com/developers/javascript-minifier/api/raw',
        {'input': js.read().decode('utf-8')},
        timeout=10
    )
    return js if minify_req is False else minify_req.text.encode('utf-8')


def minify_html(html):
    minify_req = requests.post(
        'https://www.toptal.com/developers/html-minifier/api/raw',
        {'input': html.read().decode('utf-8')},
        timeout=10
    )
    return html if minify_req is False else minify_req.text.encode('utf-8')


# gzip web files
def prepare_www_files():
    HEADER_FILE = join(env.get("PROJECT_DIR"), "include", "webFiles.h")
    filetypes_to_gzip = ["html", "css", "js"]
    data_src_dir = join(env.get("PROJECT_DIR"), "embedded_resources/web_interface")
    checksum_file = join(data_src_dir, "checksum.sha256")
    checksum = ""

    if not exists(data_src_dir):
        print(f'Error: Source directory "{data_src_dir}" does not exist!')
        return

    if exists(checksum_file):
        checksum = load_checksum_file(checksum_file)

    files_to_gzip = []
    for extension in filetypes_to_gzip:
        files_to_gzip.extend(glob.glob(join(data_src_dir, "*." + extension)))

    files_checksum = hash_files(files_to_gzip)
    if files_checksum == checksum:
        print("[GZIP & EMBED INTO HEADER] - Nothing to process.")
        return

    print(f"[GZIP & EMBED INTO HEADER] - Processing {len(files_to_gzip)} files.")

    makedirs(dirname(HEADER_FILE), exist_ok=True)

    with open(HEADER_FILE, "w") as header:
        header.write(
            "#ifndef WEB_FILES_H\n#define WEB_FILES_H\n\n#include <Arduino.h>\n\n"
        )
        header.write(
            "// THIS FILE IS AUTOGENERATED DO NOT MODIFY IT. MODIFY FILES IN /embedded_resources/web_interface\n\n"
        )

        for file in files_to_gzip:
            gz_file = file + ".gz"
            with open(file, "rb") as src, gzip.open(gz_file, "wb") as dst:
                ext = basename(file).rsplit(".", 1)[-1].lower()
                if ext == 'html':
                    minified = minify_html(src)
                elif ext == 'css':
                    minified = minify_css(src)
                elif ext == 'js':
                    minified = minify_js(src)
                else:
                    raise ValueError(f"Unsupported file type: {ext}")

                # # Output minified file
                # min_file = file + ".min"
                # with open(min_file, "wb") as minf:
                #     minf.write(minified)

                dst.write(minified)

            with open(gz_file, "rb") as gz:
                compressed_data = gz.read()
                var_name = basename(file).replace(".", "_")

                header.write(f"const uint8_t {var_name}[] PROGMEM = {{\n")

                # Write hex values, inserting a newline every 15 bytes
                for i in range(0, len(compressed_data), 15):
                    hex_chunk = ", ".join(
                        f"0x{byte:02X}" for byte in compressed_data[i : i + 15]
                    )
                    header.write(f"  {hex_chunk},\n")

                header.write("};\n\n")
                header.write(
                    f"const uint32_t {var_name}_size = {len(compressed_data)};\n\n"
                )

            remove(gz_file)  # Clean up temporary gzip file

        header.write("#endif // WEB_FILES_H\n")

    save_checksum_file(files_checksum, checksum_file)

    print(f"[DONE] Gzipped files embedded into {HEADER_FILE}")


prepare_www_files()
