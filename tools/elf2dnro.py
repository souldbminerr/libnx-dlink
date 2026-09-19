#!/usr/bin/env python3
"""elf2dnro - build a DNRO (Dynamic Nintendo Relocatable Object) from an ELF.
Usage: elf2dnro.py <input.elf> <output.dnro>
"""
import struct
import sys

PAGE = 0x1000
NRO_MAGIC = 0x304F524E
DNRO_TAG = b"DNRO"
DNRO_FLAG = 1 << 31


def align_up(x, a=PAGE):
    return (x + a - 1) & ~(a - 1)


def fail(msg):
    print("elf2dnro: error: %s" % msg, file=sys.stderr)
    sys.exit(1)


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1])
        sys.exit(2)
    elf_path, out_path = sys.argv[1], sys.argv[2]
    with open(elf_path, "rb") as f:
        e = f.read()

    if e[0:4] != b"\x7fELF" or e[4] != 2 or e[5] != 1 or e[6] != 1:
        fail("not a 64-bit little-endian version-1 ELF")
    e_machine, = struct.unpack_from("<H", e, 0x12)
    if e_machine != 0xB7:
        fail("not AArch64 (e_machine=%#x)" % e_machine)
    e_phoff, e_phentsize, e_phnum = struct.unpack_from("<QHH", e, 0x20)[0], struct.unpack_from("<H", e, 0x36)[0], struct.unpack_from("<H", e, 0x38)[0]

    loads = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from("<IIQQQQQQ", e, o)
        if p_type == 1:
            loads.append((p_vaddr, p_offset, p_filesz, p_memsz, p_flags))

    text = ro = rw = None
    for seg in loads:
        vaddr, offset, filesz, memsz, flags = seg
        if flags & 1:
            if text is not None:
                fail("multiple executable segments")
            text = seg
        elif flags & 2:
            if rw is not None:
                fail("multiple writable segments")
            rw = seg
        else:
            if ro is not None:
                fail("multiple read-only segments")
            ro = seg
    if text is None or ro is None or rw is None:
        fail("need RX, R and RW segments (got %d LOADs)" % len(loads))
    if text[0] != 0:
        fail("text segment must start at VA 0 (NRO layout)")
    for name, seg in (("text", text), ("ro", ro), ("rw", rw)):
        if seg[0] % PAGE or seg[1] % PAGE:
            fail("%s segment not page-aligned" % name)

    t_end = align_up(text[2])
    ro_end = align_up(ro[0] + ro[2])
    rw_end = align_up(rw[0] + rw[2])
    bss_size = align_up(rw[0] + rw[3]) - rw_end
    image_size = rw_end

    img = bytearray(image_size)
    for vaddr, offset, filesz, memsz, flags in (text, ro, rw):
        img[vaddr:vaddr + filesz] = e[offset:offset + filesz]

    if img[0x10:0x14] == struct.pack("<I", NRO_MAGIC):
        fail("linked image already contains an NRO header")
    mod_off, = struct.unpack_from("<I", img, 4)
    if img[mod_off:mod_off + 4] != b"MOD0":
        fail("no MOD0 header at mod_offset %#x" % mod_off)

    hdr = bytearray(0x80)
    struct.pack_into("<I", hdr, 0x00, NRO_MAGIC)
    struct.pack_into("<I", hdr, 0x08, image_size)
    struct.pack_into("<I", hdr, 0x0C, DNRO_FLAG)
    struct.pack_into("<II", hdr, 0x10, text[0], t_end - text[0])
    struct.pack_into("<II", hdr, 0x18, ro[0], ro_end - ro[0])
    struct.pack_into("<II", hdr, 0x20, rw[0], rw_end - rw[0])
    struct.pack_into("<I", hdr, 0x28, bss_size)
    hdr[0x2C:0x30] = DNRO_TAG
    img[0x10:0x90] = hdr

    with open(out_path, "wb") as f:
        f.write(img)
    print("elf2dnro: %s: image %#x, bss %#x" % (out_path, image_size, bss_size))


main()
