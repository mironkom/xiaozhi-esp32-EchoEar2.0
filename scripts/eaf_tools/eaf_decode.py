import struct
import sys
from pathlib import Path
from PIL import Image

BG_COLOR = (0, 0, 0)  # matches EMOTE_DEF_BG_COLOR 0x000000


def decode_rle(payload):
    out = bytearray()
    i = 0
    n = len(payload)
    while i + 1 < n:
        count = payload[i]
        value = payload[i + 1]
        i += 2
        out.extend([value] * count)
    return bytes(out)


def decode_raw(payload):
    return payload


def decode_huffman(payload):
    if len(payload) < 3:
        return b""
    dict_size = payload[0] | (payload[1] << 8)
    dict_data = payload[2:2 + dict_size]
    encoded = payload[2 + dict_size:]

    if len(encoded) == 0:
        # single-symbol special case
        pos = 1
        symbols = []
        while pos < len(dict_data):
            sym = dict_data[pos]; pos += 1
            code_len = dict_data[pos]; pos += 1
            code_byte_len = (code_len + 7) // 8
            pos += code_byte_len
            symbols.append(sym)
        return bytes([symbols[0]]) if symbols else b""

    padding_bits = dict_data[0]
    pos = 1
    # build tree as dict: node id -> (left, right, is_leaf, symbol)
    tree = {}
    root = 0
    tree[root] = [None, None, False, 0]
    next_id = 1
    while pos < len(dict_data):
        symbol = dict_data[pos]; pos += 1
        code_len = dict_data[pos]; pos += 1
        code_byte_len = (code_len + 7) // 8
        code = 0
        for _ in range(code_byte_len):
            code = (code << 8) | dict_data[pos]; pos += 1
        cur = root
        for bit_pos in range(code_len - 1, -1, -1):
            bit = (code >> bit_pos) & 1
            side = 1 if bit else 0
            if tree[cur][side] is None:
                tree[next_id] = [None, None, False, 0]
                tree[cur][side] = next_id
                next_id += 1
            cur = tree[cur][side]
        tree[cur][2] = True
        tree[cur][3] = symbol

    total_bits = len(encoded) * 8
    if padding_bits > 0:
        total_bits -= padding_bits

    out = bytearray()
    cur = root
    for bit_index in range(total_bits):
        byte_idx = bit_index // 8
        bit_offset = 7 - (bit_index % 8)
        bit_val = (encoded[byte_idx] >> bit_offset) & 1
        node = tree[cur]
        cur = node[1] if bit_val else node[0]
        if cur is None:
            break
        node = tree[cur]
        if node[2]:
            out.append(node[3])
            cur = root
    return bytes(out)


def decode_huffman_rle(payload):
    return decode_rle(decode_huffman(payload))


DECODERS = {
    0: decode_rle,
    1: decode_huffman_rle,
    2: None,   # JPEG - not implemented
    3: decode_huffman,
    4: None,   # heatshrink - not implemented
    5: decode_raw,
}


def get_palette_color(palette, index):
    off = index * 4
    b, g, r, a = palette[off:off + 4]
    if b == 0 and g == 0 and r == 0 and a == 0:
        return None  # transparent
    return (r, g, b)


def decode_frame(sub):
    fmt = sub[0:2]
    if fmt != b"_S":
        return None
    bit_depth = sub[9]
    width, height, blocks, block_height = struct.unpack_from("<HHHH", sub, 10)
    block_len = struct.unpack_from("<%dI" % blocks, sub, 18)
    if bit_depth == 24:
        palette = None
        num_colors = 0
    else:
        num_colors = 1 << bit_depth
        palette_off = 18 + blocks * 4
        palette = sub[palette_off:palette_off + num_colors * 4]

    data_offset = 18 + blocks * 4 + num_colors * 4
    offsets = [data_offset]
    for i in range(1, blocks):
        offsets.append(offsets[-1] + block_len[i - 1])

    img = Image.new("RGB", (width, height), BG_COLOR)
    px = img.load()

    for b in range(blocks):
        block_data = sub[offsets[b]:offsets[b] + block_len[b]]
        encoding_type = block_data[0]
        payload = block_data[1:]
        decoder = DECODERS.get(encoding_type)
        if decoder is None:
            continue
        decoded = decoder(payload)

        row0 = b * block_height
        rows_here = min(block_height, height - row0)
        needed = width * rows_here
        if len(decoded) < needed:
            continue

        if bit_depth == 8:
            for ry in range(rows_here):
                base = ry * width
                y = row0 + ry
                for x in range(width):
                    idx = decoded[base + x]
                    color = get_palette_color(palette, idx)
                    if color is not None:
                        px[x, y] = color
        # bit_depth 4/24 not needed for this asset set

    return img


def decode_eaf(path: Path):
    data = path.read_bytes()
    if data[0] != 0x89:
        raise ValueError("bad magic")
    total_frames = struct.unpack_from("<i", data, 4)[0]
    table_offset = 16
    entries = []
    for i in range(total_frames):
        frame_size, frame_offset = struct.unpack_from("<II", data, table_offset + i * 8)
        entries.append((frame_size, frame_offset))
    frame_data_base = table_offset + total_frames * 8

    frames = []
    for size, off in entries:
        frame_mem = data[frame_data_base + off: frame_data_base + off + size]
        sub = frame_mem[2:]  # skip 0x5A5A magic
        img = decode_frame(sub)
        if img is not None:
            frames.append(img)
    return frames


def main():
    src_dir = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    fps = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    out_dir.mkdir(parents=True, exist_ok=True)

    for eaf_path in sorted(src_dir.glob("*.eaf")):
        try:
            frames = decode_eaf(eaf_path)
        except Exception as e:
            print(f"FAILED {eaf_path.name}: {e}")
            continue
        if not frames:
            print(f"EMPTY {eaf_path.name}")
            continue
        out_path = out_dir / (eaf_path.stem + ".gif")
        duration_ms = int(1000 / fps)
        frames[0].save(
            out_path, save_all=True, append_images=frames[1:],
            duration=duration_ms, loop=0, optimize=False
        )
        print(f"OK {eaf_path.name} -> {out_path.name} ({len(frames)} frames, {frames[0].size})")


if __name__ == "__main__":
    main()
