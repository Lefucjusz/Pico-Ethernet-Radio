import argparse
import struct
import zlib
from pathlib import Path

IMAGE_MAGIC = 0xCAFED00D
HEADER_SIZE = 256


def add_header(app_file, output_file):
    app = Path(app_file).read_bytes()

    # Strip dummy header
    app = app[HEADER_SIZE:]

    image_size = len(app)
    crc = zlib.crc32(app)

    header = struct.pack(
        "<III",
        IMAGE_MAGIC,
        image_size,
        crc
    )

    # Pad header
    header += bytes(HEADER_SIZE - len(header))

    Path(output_file).write_bytes(header + app)

    print(f"Input:  {app_file}")
    print(f"Output: {output_file}")
    print(f"Size:   {image_size} bytes")
    print(f"CRC32:  0x{crc:08X}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")

    args = parser.parse_args()

    add_header(args.input, args.output)
