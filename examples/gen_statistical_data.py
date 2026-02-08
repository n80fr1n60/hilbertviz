#!/usr/bin/env python3
"""Generate synthetic binary input data for hilbertviz using statistical modes."""

from __future__ import annotations

import argparse
import bisect
import math
import os
import random
import sys
from typing import Sequence


MODES = [
    "uniform",
    "mixed",
    "clustered",
    "bernoulli",
    "poisson",
    "binomial",
    "geometric",
    "negative_binomial",
    "normal",
    "lognormal",
    "exponential",
    "zipf",
    "beta",
    "gamma",
    "markov",
    "mixture",
]

MIXTURE_COMPONENT_MODES = {
    "uniform",
    "mixed",
    "bernoulli",
    "poisson",
    "binomial",
    "geometric",
    "negative_binomial",
    "normal",
    "lognormal",
    "exponential",
    "zipf",
    "beta",
    "gamma",
}


def parse_size(text: str) -> int:
    s = text.strip().lower()
    if not s:
        raise argparse.ArgumentTypeError("size must not be empty")

    mul = 1
    if s.endswith("k"):
        mul = 1024
        s = s[:-1]
    elif s.endswith("m"):
        mul = 1024 * 1024
        s = s[:-1]
    elif s.endswith("g"):
        mul = 1024 * 1024 * 1024
        s = s[:-1]

    try:
        base = int(s, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid size: {text}") from exc

    size = base * mul
    if size <= 0:
        raise argparse.ArgumentTypeError("size must be > 0")
    return size


def parse_probability(text: str) -> float:
    try:
        p = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid probability: {text}") from exc
    if p < 0.0 or p > 1.0:
        raise argparse.ArgumentTypeError("probability must be in [0, 1]")
    return p


def parse_positive_float(text: str) -> float:
    try:
        v = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid float: {text}") from exc
    if v <= 0.0:
        raise argparse.ArgumentTypeError("value must be > 0")
    return v


def parse_nonnegative_float(text: str) -> float:
    try:
        v = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid float: {text}") from exc
    if v < 0.0:
        raise argparse.ArgumentTypeError("value must be >= 0")
    return v


def parse_positive_int(text: str) -> int:
    try:
        v = int(text, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer: {text}") from exc
    if v <= 0:
        raise argparse.ArgumentTypeError("value must be > 0")
    return v


def parse_byte(text: str) -> int:
    try:
        value = int(text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid byte value: {text}") from exc
    if value < 0 or value > 255:
        raise argparse.ArgumentTypeError("byte value must be in [0, 255]")
    return value


def parse_csv_floats(text: str) -> list[float]:
    parts = [p.strip() for p in text.split(",")]
    if any(p == "" for p in parts):
        raise argparse.ArgumentTypeError("empty entry in comma-separated float list")
    try:
        return [float(p) for p in parts]
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid comma-separated float list: {text}") from exc


def parse_csv_strings(text: str) -> list[str]:
    parts = [p.strip() for p in text.split(",")]
    out = [p for p in parts if p]
    if not out:
        raise argparse.ArgumentTypeError("component list must not be empty")
    return out


def clamp_byte(value: int) -> int:
    if value < 0:
        return 0
    if value > 255:
        return 255
    return value


def weighted_choice_index(rng: random.Random, weights: Sequence[float]) -> int:
    total = sum(weights)
    if total <= 0.0:
        return len(weights) - 1
    r = rng.random() * total
    acc = 0.0
    for i, w in enumerate(weights):
        acc += w
        if r < acc:
            return i
    return len(weights) - 1


def sample_byte_from_bucket(rng: random.Random, bucket: int) -> int:
    # 0: zero, 1: low-control, 2: ascii, 3: high
    if bucket == 0:
        return 0x00
    if bucket == 1:
        return rng.randint(0x01, 0x1F)
    if bucket == 2:
        return rng.randint(0x20, 0x7E)
    return rng.randint(0x7F, 0xFF)


def poisson_sample(rng: random.Random, lam: float) -> int:
    if lam <= 0.0:
        return 0

    # Knuth exact method for small lambda; normal approximation for larger lambda.
    if lam < 30.0:
        limit = math.exp(-lam)
        k = 0
        prod = 1.0
        while prod > limit:
            k += 1
            prod *= rng.random()
        return k - 1

    approx = int(round(rng.gauss(lam, math.sqrt(lam))))
    if approx < 0:
        return 0
    return approx


def geometric_failures(rng: random.Random, p: float) -> int:
    # Returns failures before first success.
    if p >= 1.0:
        return 0
    if p <= 0.0:
        return 255
    u = rng.random()
    if u <= 0.0:
        u = 1e-15
    return int(math.floor(math.log(1.0 - u) / math.log(1.0 - p)))


def binomial_sample(rng: random.Random, n: int, p: float) -> int:
    s = 0
    for _ in range(n):
        if rng.random() < p:
            s += 1
    return s


def negative_binomial_failures(rng: random.Random, r_successes: int, p: float) -> int:
    # Returns failures before r successes.
    total_failures = 0
    for _ in range(r_successes):
        total_failures += geometric_failures(rng, p)
    return total_failures


def build_zipf_cdf(s: float, k_max: int) -> list[float]:
    weights = [1.0 / (float(k) ** s) for k in range(1, k_max + 1)]
    total = sum(weights)
    if total <= 0.0:
        return [1.0]
    cdf: list[float] = []
    acc = 0.0
    for w in weights:
        acc += (w / total)
        cdf.append(acc)
    cdf[-1] = 1.0
    return cdf


def sample_zipf_index(rng: random.Random, cdf: Sequence[float]) -> int:
    r = rng.random()
    return bisect.bisect_left(cdf, r) + 1  # 1..k_max


def apply_affine(raw: float, scale: float, offset: float) -> int:
    return clamp_byte(int(round((raw * scale) + offset)))


def sample_mode_byte(
    mode: str,
    args: argparse.Namespace,
    rng: random.Random,
    zipf_cdf: Sequence[float],
) -> int:
    if mode == "uniform":
        return rng.randint(0x00, 0xFF)

    if mode == "mixed":
        # Probabilities: 65% zero, 2% low, 25% ascii, 8% high.
        b = weighted_choice_index(rng, [0.65, 0.02, 0.25, 0.08])
        return sample_byte_from_bucket(rng, b)

    if mode == "bernoulli":
        return args.bernoulli_one if rng.random() < args.bernoulli_p else args.bernoulli_zero

    if mode == "poisson":
        raw = poisson_sample(rng, args.poisson_lambda)
        return apply_affine(raw, args.poisson_scale, args.poisson_offset)

    if mode == "binomial":
        raw = binomial_sample(rng, args.binomial_n, args.binomial_p)
        return apply_affine(raw, args.binomial_scale, args.binomial_offset)

    if mode == "geometric":
        raw = geometric_failures(rng, args.geometric_p)
        return apply_affine(raw, args.geometric_scale, args.geometric_offset)

    if mode == "negative_binomial":
        raw = negative_binomial_failures(rng, args.negbin_r, args.negbin_p)
        return apply_affine(raw, args.negbin_scale, args.negbin_offset)

    if mode == "normal":
        raw = rng.gauss(args.normal_mu, args.normal_sigma)
        return apply_affine(raw, args.normal_scale, args.normal_offset)

    if mode == "lognormal":
        raw = rng.lognormvariate(args.lognormal_mu, args.lognormal_sigma)
        return apply_affine(raw, args.lognormal_scale, args.lognormal_offset)

    if mode == "exponential":
        raw = rng.expovariate(args.exponential_lambda)
        return apply_affine(raw, args.exponential_scale, args.exponential_offset)

    if mode == "zipf":
        raw = sample_zipf_index(rng, zipf_cdf)
        return apply_affine(raw, args.zipf_scale, args.zipf_offset)

    if mode == "beta":
        raw = rng.betavariate(args.beta_a, args.beta_b)
        return apply_affine(raw, args.beta_scale, args.beta_offset)

    if mode == "gamma":
        raw = rng.gammavariate(args.gamma_shape, args.gamma_theta)
        return apply_affine(raw, args.gamma_byte_scale, args.gamma_offset)

    raise ValueError(f"unsupported sampling mode: {mode}")


def generate_independent_mode(
    mode: str,
    rng: random.Random,
    args: argparse.Namespace,
    size: int,
    zipf_cdf: Sequence[float],
) -> bytearray:
    out = bytearray(size)
    for i in range(size):
        out[i] = sample_mode_byte(mode, args, rng, zipf_cdf)
    return out


def generate_clustered(rng: random.Random, size: int) -> bytearray:
    bucket_weights = [0.55, 0.05, 0.30, 0.10]
    out = bytearray()

    while len(out) < size:
        bucket = weighted_choice_index(rng, bucket_weights)

        # Most blocks are moderate length, some are long to create obvious regions.
        if rng.random() < 0.15:
            run = rng.randint(4096, 16384)
        else:
            run = rng.randint(64, 2048)

        remaining = size - len(out)
        run = min(run, remaining)

        if bucket == 0:
            out.extend(b"\x00" * run)
            continue

        for _ in range(run):
            out.append(sample_byte_from_bucket(rng, bucket))

    return out


def generate_markov(rng: random.Random, size: int, stay_prob: float, init_weights: Sequence[float]) -> bytearray:
    out = bytearray(size)
    state = weighted_choice_index(rng, init_weights)
    for i in range(size):
        if i > 0 and rng.random() >= stay_prob:
            state = weighted_choice_index(rng, init_weights)
        out[i] = sample_byte_from_bucket(rng, state)
    return out


def parse_mixture_spec(args: argparse.Namespace) -> tuple[list[str], list[float]]:
    comps = parse_csv_strings(args.mixture_components)
    weights = parse_csv_floats(args.mixture_weights)

    if len(comps) != len(weights):
        raise ValueError("mixture-components and mixture-weights must have the same length")
    if any(w < 0.0 for w in weights):
        raise ValueError("mixture weights must be >= 0")
    if sum(weights) <= 0.0:
        raise ValueError("mixture weights must sum to > 0")

    for comp in comps:
        if comp not in MIXTURE_COMPONENT_MODES:
            raise ValueError(
                f"unsupported mixture component '{comp}'. "
                f"Allowed: {', '.join(sorted(MIXTURE_COMPONENT_MODES))}"
            )
    return comps, weights


def generate_mixture(
    rng: random.Random,
    args: argparse.Namespace,
    size: int,
    zipf_cdf: Sequence[float],
) -> bytearray:
    components, weights = parse_mixture_spec(args)
    out = bytearray(size)
    for i in range(size):
        idx = weighted_choice_index(rng, weights)
        out[i] = sample_mode_byte(components[idx], args, rng, zipf_cdf)
    return out


def print_stats(data: bytes) -> None:
    total = len(data)
    zero = sum(1 for b in data if b == 0x00)
    low = sum(1 for b in data if 0x01 <= b <= 0x1F)
    ascii_n = sum(1 for b in data if 0x20 <= b <= 0x7E)
    high = sum(1 for b in data if 0x7F <= b <= 0xFF)
    mean = (sum(data) / total) if total else 0.0

    def pct(n: int) -> float:
        return (100.0 * n / total) if total else 0.0

    print(f"bytes: {total}")
    print(f"mean byte value:       {mean:10.3f}")
    print(f"zero [0x00]:          {zero:10d} ({pct(zero):6.2f}%)")
    print(f"low  [0x01..0x1F]:    {low:10d} ({pct(low):6.2f}%)")
    print(f"ascii[0x20..0x7E]:    {ascii_n:10d} ({pct(ascii_n):6.2f}%)")
    print(f"high [0x7F..0xFF]:    {high:10d} ({pct(high):6.2f}%)")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate synthetic binary data for hilbertviz with statistical distributions.",
        epilog=(
            "Examples:\n"
            "  %(prog)s -o non_uniform.bin --size 1m --mode clustered --overwrite\n"
            "  %(prog)s -o bern.bin --mode bernoulli --bernoulli-p 0.05 --overwrite\n"
            "  %(prog)s -o pois.bin --mode poisson --poisson-lambda 22 --poisson-scale 2 --overwrite\n"
            "  %(prog)s -o mix.bin --mode mixture "
            "--mixture-components normal,zipf,bernoulli "
            "--mixture-weights 0.5,0.3,0.2 --overwrite"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-o",
        "--output",
        default="synthetic.bin",
        help="output file path (default: synthetic.bin)",
    )
    parser.add_argument(
        "-s",
        "--size",
        type=parse_size,
        default=parse_size("1m"),
        help="output size in bytes; suffixes k/m/g supported (default: 1m)",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="PRNG seed (default: 42)",
    )
    parser.add_argument(
        "--mode",
        choices=MODES,
        default="clustered",
        help="generation mode (default: clustered)",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="overwrite output file if it already exists",
    )
    parser.add_argument(
        "--no-stats",
        action="store_true",
        help="suppress terminal distribution summary",
    )

    # Bernoulli.
    parser.add_argument("--bernoulli-p", type=parse_probability, default=0.12, help="P(X=one-byte) for bernoulli mode")
    parser.add_argument("--bernoulli-zero", type=parse_byte, default=0x00, help="byte for bernoulli zero outcome")
    parser.add_argument("--bernoulli-one", type=parse_byte, default=0xFF, help="byte for bernoulli one outcome")

    # Poisson.
    parser.add_argument("--poisson-lambda", type=parse_nonnegative_float, default=24.0, help="lambda for poisson mode")
    parser.add_argument("--poisson-scale", type=float, default=1.0, help="scale for poisson sample mapping")
    parser.add_argument("--poisson-offset", type=float, default=0.0, help="offset for poisson sample mapping")

    # Binomial.
    parser.add_argument("--binomial-n", type=parse_positive_int, default=32, help="n for binomial mode")
    parser.add_argument("--binomial-p", type=parse_probability, default=0.3, help="p for binomial mode")
    parser.add_argument("--binomial-scale", type=float, default=6.0, help="scale for binomial sample mapping")
    parser.add_argument("--binomial-offset", type=float, default=0.0, help="offset for binomial sample mapping")

    # Geometric.
    parser.add_argument("--geometric-p", type=parse_probability, default=0.08, help="p for geometric mode")
    parser.add_argument("--geometric-scale", type=float, default=6.0, help="scale for geometric sample mapping")
    parser.add_argument("--geometric-offset", type=float, default=0.0, help="offset for geometric sample mapping")

    # Negative binomial.
    parser.add_argument("--negbin-r", type=parse_positive_int, default=4, help="required successes r for negative_binomial mode")
    parser.add_argument("--negbin-p", type=parse_probability, default=0.15, help="success probability p for negative_binomial mode")
    parser.add_argument("--negbin-scale", type=float, default=3.0, help="scale for negative_binomial sample mapping")
    parser.add_argument("--negbin-offset", type=float, default=0.0, help="offset for negative_binomial sample mapping")

    # Normal.
    parser.add_argument("--normal-mu", type=float, default=128.0, help="mu for normal mode")
    parser.add_argument("--normal-sigma", type=parse_positive_float, default=35.0, help="sigma for normal mode")
    parser.add_argument("--normal-scale", type=float, default=1.0, help="scale for normal sample mapping")
    parser.add_argument("--normal-offset", type=float, default=0.0, help="offset for normal sample mapping")

    # Lognormal.
    parser.add_argument("--lognormal-mu", type=float, default=3.2, help="mu for lognormal mode")
    parser.add_argument("--lognormal-sigma", type=parse_positive_float, default=0.9, help="sigma for lognormal mode")
    parser.add_argument("--lognormal-scale", type=float, default=10.0, help="scale for lognormal sample mapping")
    parser.add_argument("--lognormal-offset", type=float, default=0.0, help="offset for lognormal sample mapping")

    # Exponential.
    parser.add_argument("--exponential-lambda", type=parse_positive_float, default=0.08, help="lambda for exponential mode")
    parser.add_argument("--exponential-scale", type=float, default=16.0, help="scale for exponential sample mapping")
    parser.add_argument("--exponential-offset", type=float, default=0.0, help="offset for exponential sample mapping")

    # Zipf / power-law.
    parser.add_argument("--zipf-s", type=parse_positive_float, default=1.3, help="exponent s for zipf mode")
    parser.add_argument("--zipf-k-max", type=parse_positive_int, default=64, help="max rank k for zipf mode")
    parser.add_argument("--zipf-scale", type=float, default=4.0, help="scale for zipf rank mapping")
    parser.add_argument("--zipf-offset", type=float, default=0.0, help="offset for zipf rank mapping")

    # Beta.
    parser.add_argument("--beta-a", type=parse_positive_float, default=0.8, help="alpha for beta mode")
    parser.add_argument("--beta-b", type=parse_positive_float, default=2.8, help="beta for beta mode")
    parser.add_argument("--beta-scale", type=float, default=255.0, help="scale for beta sample mapping")
    parser.add_argument("--beta-offset", type=float, default=0.0, help="offset for beta sample mapping")

    # Gamma.
    parser.add_argument("--gamma-shape", type=parse_positive_float, default=2.0, help="shape k for gamma mode")
    parser.add_argument("--gamma-theta", type=parse_positive_float, default=18.0, help="theta scale for gamma mode")
    parser.add_argument("--gamma-byte-scale", type=float, default=1.0, help="scale for gamma sample mapping")
    parser.add_argument("--gamma-offset", type=float, default=0.0, help="offset for gamma sample mapping")

    # Markov buckets.
    parser.add_argument("--markov-stay", type=parse_probability, default=0.965, help="state stay probability for markov mode")
    parser.add_argument(
        "--markov-init-weights",
        default="0.55,0.05,0.30,0.10",
        help="comma-separated initial bucket weights for markov mode (4 values: zero,low,ascii,high)",
    )

    # Mixture.
    parser.add_argument(
        "--mixture-components",
        default="normal,zipf,bernoulli",
        help="comma-separated component modes for mixture mode",
    )
    parser.add_argument(
        "--mixture-weights",
        default="0.5,0.3,0.2",
        help="comma-separated component weights for mixture mode",
    )

    return parser


def validate_args(args: argparse.Namespace) -> None:
    if args.geometric_p <= 0.0:
        raise ValueError("geometric-p must be > 0")
    if args.negbin_p <= 0.0:
        raise ValueError("negbin-p must be > 0")

    markov_weights = parse_csv_floats(args.markov_init_weights)
    if len(markov_weights) != 4:
        raise ValueError("markov-init-weights must have 4 comma-separated values")
    if any(w < 0.0 for w in markov_weights):
        raise ValueError("markov-init-weights values must be >= 0")
    if sum(markov_weights) <= 0.0:
        raise ValueError("markov-init-weights sum must be > 0")
    args._markov_weights = markov_weights


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        validate_args(args)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    if os.path.exists(args.output) and not args.overwrite:
        print(
            f"refusing to overwrite existing file: {args.output}\n"
            "use --overwrite to replace it",
            file=sys.stderr,
        )
        return 1

    rng = random.Random(args.seed)
    zipf_cdf = build_zipf_cdf(args.zipf_s, args.zipf_k_max)

    if args.mode in MIXTURE_COMPONENT_MODES:
        data = generate_independent_mode(args.mode, rng, args, args.size, zipf_cdf)
    elif args.mode == "clustered":
        data = generate_clustered(rng, args.size)
    elif args.mode == "markov":
        data = generate_markov(rng, args.size, args.markov_stay, args._markov_weights)
    elif args.mode == "mixture":
        try:
            data = generate_mixture(rng, args, args.size, zipf_cdf)
        except ValueError as exc:
            print(str(exc), file=sys.stderr)
            return 1
    else:
        print(f"unsupported mode: {args.mode}", file=sys.stderr)
        return 1

    with open(args.output, "wb") as f:
        f.write(data)

    print(
        f"wrote {len(data)} bytes to {args.output} "
        f"(mode={args.mode}, seed={args.seed})"
    )
    if not args.no_stats:
        print_stats(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
