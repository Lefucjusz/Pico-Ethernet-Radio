import argparse
import subprocess
from pathlib import Path

BOOTLOADER_OFFSET = 0
BOOTLOADER_SIZE = 64 * 1024
APP_OFFSET = BOOTLOADER_OFFSET + BOOTLOADER_SIZE
APP_SIZE = 512 * 1024

ERASE_VALUE = b"\xFF"

def create_factory_bin(bootloader_file, app_file, output_file):
    bootloader_data = Path(bootloader_file).read_bytes()
    app_data = Path(app_file).read_bytes()

    if len(bootloader_data) > BOOTLOADER_SIZE:
        raise ValueError("Bootloader exceeds slot size")

    if len(app_data) > APP_SIZE:
        raise ValueError("Application exceeds slot size")

    padding = ERASE_VALUE * (BOOTLOADER_SIZE - len(bootloader_data))

    factory = (bootloader_data + padding + app_data)

    Path(output_file).write_bytes(factory)

    print(f"Binary size:    {len(factory)} bytes")


def create_factory_uf2(bin_file, uf2_file):
    subprocess.run(["picotool", "uf2", "convert", bin_file, uf2_file, "--family", "rp2040"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument("bootloader")
    parser.add_argument("app")
    parser.add_argument("output_bin")
    parser.add_argument("output_uf2")

    args = parser.parse_args()

    create_factory_bin(args.bootloader, args.app, args.output_bin)
    create_factory_uf2(args.output_bin, args.output_uf2)

    print(f"Output binary:  {args.output_bin}")
    print(f"Output UF2:     {args.output_uf2}")
