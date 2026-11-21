#!/usr/bin/env python3
"""GGUF file inspector for basalt model compatibility work.

Usage:
    gguf_info.py [--tensors] [--check KEY [KEY ...]] file [file ...]

Default output: architecture, name, tensor count, type breakdown,
non-block tensors, block weight types, and basalt kernel compatibility.
"""
from __future__ import annotations

import argparse
import pathlib
import sys
from collections import Counter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "engine" / "basalt" / "tools"))
from gguf_reader import GGUFFile, GGML_TYPES  # noqa: E402

# Types the basalt dense kernels natively read for linear layers
DENSE_KERNEL_TYPES = {"Q8_0", "NVFP4_PLANES", "F8_E4M3"}
# Types accepted for token_embd (native or dequantizable to F16)
EMBD_TYPES = {"BF16", "F16", "Q4_K", "Q8_0"}


def fmt_shape(shape: list[int]) -> str:
    return " x ".join(str(d) for d in shape)


def fmt_elements(n: int) -> str:
    if n >= 1_000_000_000:
        return f"{n / 1_000_000_000:.2f}B"
    if n >= 1_000_000:
        return f"{n / 1_000_000:.1f}M"
    if n >= 1_000:
        return f"{n / 1_000:.1f}K"
    return str(n)


def summarize(g: GGUFFile) -> None:
    name = g.metadata.get("general.name", "?")
    arch = g.metadata.get("general.architecture", "?")
    n_tensors = len(g.tensors)
    file_size = g.path.stat().st_size

    print(f"=== {g.path.name} ===")
    print(f"  file:      {g.path} ({file_size / 1e9:.2f} GB)")
    print(f"  arch:      {arch}")
    print(f"  name:      {name}")
    print(f"  tensors:   {n_tensors}")
    print()

    # Type breakdown
    type_counts = g.by_type()
    print("  tensor types:")
    for tname, count in type_counts.items():
        marker = ""
        if tname in DENSE_KERNEL_TYPES:
            marker = "  [dense-kernel]"
        elif tname in EMBD_TYPES:
            marker = "  [embd-ok]"
        print(f"    {tname:<16} {count:>5}{marker}")
    print()

    # Non-block tensors (the critical ones for compatibility)
    non_blk = [t for t in g.tensors if not t.name.startswith("blk.")]
    if non_blk:
        print(f"  non-block tensors ({len(non_blk)}):")
        for t in non_blk:
            print(f"    {t.name:<40} {t.type_name:<16} {fmt_shape(t.shape)}  ({fmt_elements(t.elements)} elems)")
        print()

    # Block weight types: separate large linear weights from small F32 norms/biases/SSM params
    # Linear weights (through linear()) need dense kernel types; small F32 tensors are fine
    LARGE_ELEM_THRESHOLD = 1_000_000  # tensors above this are linear layer weights
    blk_linear = [t for t in g.tensors if t.name.startswith("blk.") and t.elements >= LARGE_ELEM_THRESHOLD]
    blk_small = [t for t in g.tensors if t.name.startswith("blk.") and t.elements < LARGE_ELEM_THRESHOLD]
    blk_linear_types = Counter(t.type_name for t in blk_linear)
    blk_small_types = Counter(t.type_name for t in blk_small)
    if blk_linear_types:
        print("  block linear weights (dense kernels):")
        for tname, count in blk_linear_types.most_common():
            ok = "OK" if tname in DENSE_KERNEL_TYPES else "NOT SUPPORTED"
            print(f"    {tname:<16} {count:>5}  [{ok}]")
        print()
    if blk_small_types:
        print("  block small tensors (norms/biases/SSM, via floats):")
        for tname, count in blk_small_types.most_common():
            print(f"    {tname:<16} {count:>5}")
        print()

    # Compatibility verdict
    unsupported_blk = {t for t in blk_linear_types if t not in DENSE_KERNEL_TYPES}
    embd = g.find("token_embd.weight")
    embd_type = embd[0].type_name if embd else "?"
    out = g.find("output.weight")
    out_type = out[0].type_name if out else "?"

    print("  basalt compatibility:")
    print(f"    embedding:  {embd_type}  {'OK' if embd_type in EMBD_TYPES else 'NOT SUPPORTED'}")
    print(f"    output:     {out_type}  {'OK' if out_type in DENSE_KERNEL_TYPES else 'NOT SUPPORTED'}")
    if unsupported_blk:
        print(f"    layers:     {', '.join(sorted(unsupported_blk))}  NOT SUPPORTED")
    else:
        print(f"    layers:     OK")
    compatible = not unsupported_blk and out_type in DENSE_KERNEL_TYPES and embd_type in EMBD_TYPES
    print(f"    verdict:    {'COMPATIBLE' if compatible else 'INCOMPATIBLE'}")
    print()


def list_tensors(g: GGUFFile) -> None:
    print(f"  all tensors ({len(g.tensors)}):")
    for t in g.tensors:
        print(f"    {t.name:<50} {t.type_name:<16} {fmt_shape(t.shape)}")
    print()


def check_keys(g: GGUFFile, keys: list[str]) -> None:
    names = set(t.name for t in g.tensors)
    print(f"  key check:")
    for k in keys:
        status = "PRESENT" if k in names else "MISSING"
        print(f"    {k:<45} {status}")
    print()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Inspect GGUF files for basalt model compatibility.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("files", nargs="+", help="one or more .gguf file paths")
    parser.add_argument("--tensors", action="store_true", help="list all tensors with type and shape")
    parser.add_argument("--check", nargs="+", metavar="KEY",
                        help="check for specific tensor names (e.g. token_embd.weight output.weight)")
    args = parser.parse_args()

    for path_str in args.files:
        path = pathlib.Path(path_str)
        if not path.exists():
            print(f"ERROR: {path} not found", file=sys.stderr)
            continue
        try:
            g = GGUFFile(path)
        except Exception as e:
            print(f"ERROR: {path.name}: {e}", file=sys.stderr)
            continue

        summarize(g)
        if args.tensors:
            list_tensors(g)
        if args.check:
            check_keys(g, args.check)


if __name__ == "__main__":
    main()