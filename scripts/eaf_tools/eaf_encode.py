import struct
from PIL import Image


def rle_encode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        v = data[i]
        j = i + 1
        while j < n and data[j] == v and (j - i) < 255:
            j += 1
        out.append(j - i)
        out.append(v)
        i = j
    return bytes(out)


def quantize_frame(img_rgba: Image.Image, colors=255):
    """Return (index_bytes, palette_bytes[256*4]) using index 255 as transparent sentinel."""
    w, h = img_rgba.size
    rgba = img_rgba.convert("RGBA")
    alpha = rgba.split()[3]
    rgb = rgba.convert("RGB")

    quant = rgb.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = quant.getpalette()  # flat list r,g,b,r,g,b,...
    idx_data = bytearray(quant.tobytes())
    alpha_data = alpha.tobytes()

    TRANSPARENT_IDX = 255
    for i in range(w * h):
        if alpha_data[i] < 128:
            idx_data[i] = TRANSPARENT_IDX

    palette = bytearray(256 * 4)
    num_used = len(pal) // 3
    for i in range(min(num_used, 255)):
        r, g, b = pal[i * 3:i * 3 + 3]
        off = i * 4
        palette[off + 0] = b
        palette[off + 1] = g
        palette[off + 2] = r
        palette[off + 3] = 255
    # index 255 stays all-zero -> transparent sentinel

    return bytes(idx_data), bytes(palette)


def build_frame_blob(img_rgba: Image.Image) -> bytes:
    w, h = img_rgba.size
    idx_data, palette = quantize_frame(img_rgba)

    encoding_type = 0  # RLE
    rle_payload = rle_encode(idx_data)
    block_data = bytes([encoding_type]) + rle_payload
    block_len = len(block_data)

    blocks = 1
    block_height = h
    bit_depth = 8

    sub = bytearray()
    sub += b"_S"
    sub += b"\x00"          # byte[2], unused by parser
    sub += b"GEN000"        # bytes[3:9] version, unused by parser (6 bytes)
    sub.append(bit_depth)   # byte[9]
    sub += struct.pack("<HH", w, h)
    sub += struct.pack("<HH", blocks, block_height)
    sub += struct.pack("<I", block_len)   # block_len table (1 entry)
    sub += palette                          # 256*4 bytes
    sub += block_data

    frame_mem = struct.pack("<H", 0x5A5A) + bytes(sub)
    return frame_mem


def encode_eaf(frames, fps=20) -> bytes:
    frame_blobs = [build_frame_blob(f) for f in frames]
    total_frames = len(frame_blobs)

    table = bytearray()
    offset = 0
    for blob in frame_blobs:
        table += struct.pack("<II", len(blob), offset)
        offset += len(blob)

    body = table + b"".join(frame_blobs)
    stored_len = len(body)
    checksum = sum(body) & 0xFFFFFFFF

    header = bytearray()
    header.append(0x89)
    header += b"EAF"
    header += struct.pack("<i", total_frames)
    header += struct.pack("<I", checksum)
    header += struct.pack("<I", stored_len)

    return bytes(header) + body
