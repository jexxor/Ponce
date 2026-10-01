"""Create an IDA 9.3 Ponce release ZIP containing its platform plugin."""

import argparse
from pathlib import Path
import re
import zipfile


INSTALL_DIR = {
    "linux-x86_64": "~/.idapro/plugins/",
    "macos-x86_64": "~/.idapro/plugins/",
    "windows-x86_64": "%APPDATA%\\Hex-Rays\\IDA Pro\\plugins\\",
}
EXTENSION = {"linux-x86_64": ".so", "macos-x86_64": ".dylib", "windows-x86_64": ".dll"}


def package(binary, platform, version, output_dir, vcpkg_installed=None):
    binary = Path(binary)
    if platform not in INSTALL_DIR:
        raise ValueError("Unsupported platform: " + platform)
    if binary.name != "Ponce64" + EXTENSION[platform] or not binary.is_file():
        raise ValueError("Missing platform plugin: " + str(binary))
    safe_version = re.sub(r"[^A-Za-z0-9._-]", "-", version)
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    destination = output_dir / f"Ponce-ida9.3-{safe_version}-{platform}.zip"
    instructions = (
        "Ponce for IDA 9.3 (64-bit IDA, x86/x64 binaries)\n\n"
        f"Copy {binary.name} into {INSTALL_DIR[platform]} and restart IDA.\n"
        "This build links Triton, Capstone, and Z3 statically; IDA supplies its own libida.\n"
        "IDA menu: Edit > Ponce. Hex-Rays annotations activate when decompiler is installed.\n"
    )
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.write(binary, binary.name)
        archive.write("LICENSE", "LICENSE")
        archive.writestr("INSTALL.txt", instructions)
        if vcpkg_installed is not None:
            for dependency in ("triton", "capstone", "z3"):
                license_file = Path(vcpkg_installed) / "share" / dependency / "copyright"
                archive.write(license_file, f"LICENSES/{dependency}.txt")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--platform", required=True, choices=sorted(INSTALL_DIR))
    parser.add_argument("--version", required=True)
    parser.add_argument("--output-dir", default="dist")
    parser.add_argument("--vcpkg-installed", help="Triplet's installed libraries (include their licenses)")
    args = parser.parse_args()
    print(package(args.binary, args.platform, args.version, args.output_dir, args.vcpkg_installed))
