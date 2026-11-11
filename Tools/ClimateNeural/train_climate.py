#!/usr/bin/env python3
"""Train a compact coordinate network on an RG climate reference image.

Image convention:
    R = temperature
    G = humidity

The exported GLSL network returns vec2(humidity, temperature). Training uses
random 512x512 crops with global image coordinates, plus deterministic Perlin
features matching Asset/Shader/Noise/neural_climate_2d.glsl.
"""
from __future__ import annotations

import argparse
import json
import math
import random
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

try:
    import numpy as np
    from PIL import Image
    import torch
    import torch.nn as nn
    import torch.nn.functional as F
except ImportError as error:
    print(
        "Missing dependency. Install with:\n"
        "  python -m pip install -r Tools/ClimateNeural/requirements.txt",
        file=sys.stderr,
    )
    raise SystemExit(2) from error


MASK32 = 0xFFFFFFFF
FEATURE_COUNT = 16
PERLIN_SCALES = (1.0, 2.0, 4.0, 8.0)
PERLIN_SALTS = (0x243F6A88, 0x85A308D3, 0x13198A2E, 0x03707344)


@dataclass
class TrainingConfig:
    image: str
    output: str
    checkpoint: str
    preview: str
    steps: int
    crop_size: int
    grid_size: int
    hidden_width: int
    learning_rate: float
    edge_weight: float
    flat_weight: float
    flat_threshold: float
    feature_seed: int
    random_seed: int
    preview_every: int
    device: str


class ClimateNetwork(nn.Module):
    def __init__(self, hidden_width: int) -> None:
        super().__init__()
        if hidden_width < 2:
            raise ValueError("hidden_width must be at least 2")
        self.hidden_width = hidden_width
        self.layer0 = nn.Linear(FEATURE_COUNT, hidden_width)
        self.layer1 = nn.Linear(hidden_width, hidden_width)
        self.output = nn.Linear(hidden_width, 2)
        for layer in (self.layer0, self.layer1, self.output):
            nn.init.xavier_uniform_(layer.weight)
            nn.init.zeros_(layer.bias)

    def forward(self, features: torch.Tensor) -> torch.Tensor:
        values = torch.tanh(self.layer0(features))
        values = torch.tanh(self.layer1(values))
        return torch.sigmoid(self.output(values))


def uint32(values: torch.Tensor) -> torch.Tensor:
    return torch.bitwise_and(values, MASK32)


def noise_perlin_2d(points: torch.Tensor, seed: int) -> torch.Tensor:
    """Vectorized copy of noisePerlin2D used by the GLSL runtime."""
    cell = torch.floor(points).to(torch.int64)
    fraction = points - torch.floor(points)
    fade = fraction * fraction * fraction * (
        fraction * (fraction * 6.0 - 15.0) + 10.0
    )

    offsets = torch.tensor(
        ((0, 0), (1, 0), (0, 1), (1, 1)),
        dtype=torch.int64,
        device=points.device,
    )
    corners = cell.unsqueeze(-2) + offsets
    hx = uint32(corners[..., 0])
    hy = uint32(corners[..., 1])
    h = torch.bitwise_xor(
        torch.bitwise_xor(uint32(hx * 0x9E3779B9), uint32(hy * 0x85EBCA6B)),
        seed & MASK32,
    )
    h = torch.bitwise_xor(h, torch.bitwise_right_shift(h, 16))
    h = uint32(h * 0x7FEB352D)
    h = torch.bitwise_xor(h, torch.bitwise_right_shift(h, 15))
    h = uint32(h * 0x846CA68B)
    h = torch.bitwise_xor(h, torch.bitwise_right_shift(h, 16))

    gradient = torch.bitwise_and(h, 7)
    dx = torch.stack(
        (
            fraction[..., 0],
            fraction[..., 0] - 1.0,
            fraction[..., 0],
            fraction[..., 0] - 1.0,
        ),
        dim=-1,
    )
    dy = torch.stack(
        (
            fraction[..., 1],
            fraction[..., 1],
            fraction[..., 1] - 1.0,
            fraction[..., 1] - 1.0,
        ),
        dim=-1,
    )
    a = torch.where(gradient >= 6, dy, dx)
    b = torch.where(gradient < 4, dy, torch.zeros_like(dy))
    signed_a = torch.where(torch.bitwise_and(gradient, 1) != 0, -a, a)
    signed_b = torch.where(torch.bitwise_and(gradient, 2) != 0, -b, b)
    values = torch.where(gradient >= 4, signed_a, signed_a + signed_b)

    row0 = torch.lerp(values[..., 0], values[..., 1], fade[..., 0])
    row1 = torch.lerp(values[..., 2], values[..., 3], fade[..., 0])
    return torch.clamp(torch.lerp(row0, row1, fade[..., 1]), -1.0, 1.0)


def climate_features(points: torch.Tensor, seed: int) -> torch.Tensor:
    """16 features shared exactly with neural_climate_2d.glsl."""
    tau = 2.0 * math.pi
    outputs: list[torch.Tensor] = []
    for frequency in (1.0, 2.0, 4.0):
        phase = points * (tau * frequency)
        outputs.extend(
            (
                torch.sin(phase[..., 0]),
                torch.cos(phase[..., 0]),
                torch.sin(phase[..., 1]),
                torch.cos(phase[..., 1]),
            )
        )
    for scale, salt in zip(PERLIN_SCALES, PERLIN_SALTS):
        outputs.append(noise_perlin_2d(points * scale, seed ^ salt))
    return torch.stack(outputs, dim=-1)


def load_target(path: Path, device: torch.device) -> torch.Tensor:
    if not path.is_file():
        raise FileNotFoundError(f"Reference image not found: {path}")
    image = Image.open(path).convert("RGB")
    array = np.asarray(image, dtype=np.float32) / 255.0
    climate = np.stack((array[..., 1], array[..., 0]), axis=-1)
    return torch.from_numpy(climate).to(device=device)


def make_crop(
    target: torch.Tensor,
    crop_size: int,
    grid_size: int,
    generator: random.Random,
) -> tuple[torch.Tensor, torch.Tensor, tuple[int, int]]:
    height, width, _ = target.shape
    crop_size = min(crop_size, width, height)
    max_x = width - crop_size
    max_y = height - crop_size
    offset_x = generator.randint(0, max_x) if max_x > 0 else 0
    offset_y = generator.randint(0, max_y) if max_y > 0 else 0

    crop = target[
        offset_y : offset_y + crop_size,
        offset_x : offset_x + crop_size,
    ].permute(2, 0, 1).unsqueeze(0)
    resized = F.interpolate(
        crop,
        size=(grid_size, grid_size),
        mode="bilinear",
        align_corners=True,
    )[0].permute(1, 2, 0)

    xs = torch.linspace(
        float(offset_x) / max(width - 1, 1),
        float(offset_x + crop_size - 1) / max(width - 1, 1),
        grid_size,
        device=target.device,
    )
    ys = torch.linspace(
        float(offset_y) / max(height - 1, 1),
        float(offset_y + crop_size - 1) / max(height - 1, 1),
        grid_size,
        device=target.device,
    )
    yy, xx = torch.meshgrid(ys, xs, indexing="ij")
    coordinates = torch.stack((xx, yy), dim=-1)
    return coordinates, resized, (offset_x, offset_y)


def gradient_losses(
    prediction: torch.Tensor,
    target: torch.Tensor,
    flat_threshold: float,
) -> tuple[torch.Tensor, torch.Tensor]:
    pred_dx = prediction[:, 1:, :] - prediction[:, :-1, :]
    pred_dy = prediction[1:, :, :] - prediction[:-1, :, :]
    target_dx = target[:, 1:, :] - target[:, :-1, :]
    target_dy = target[1:, :, :] - target[:-1, :, :]

    edge = F.l1_loss(pred_dx, target_dx) + F.l1_loss(pred_dy, target_dy)
    flat_x = (target_dx.abs().amax(dim=-1, keepdim=True) < flat_threshold).to(
        prediction.dtype
    )
    flat_y = (target_dy.abs().amax(dim=-1, keepdim=True) < flat_threshold).to(
        prediction.dtype
    )
    flat = (
        (pred_dx.abs() * flat_x).sum() / flat_x.sum().clamp_min(1.0)
        + (pred_dy.abs() * flat_y).sum() / flat_y.sum().clamp_min(1.0)
    )
    return edge, flat


def evaluate_image(
    model: ClimateNetwork,
    width: int,
    height: int,
    seed: int,
    device: torch.device,
    row_block: int = 128,
) -> np.ndarray:
    model.eval()
    result = np.empty((height, width, 2), dtype=np.float32)
    xs = torch.linspace(0.0, 1.0, width, device=device)
    with torch.no_grad():
        for first_y in range(0, height, row_block):
            count = min(row_block, height - first_y)
            ys = torch.linspace(
                float(first_y) / max(height - 1, 1),
                float(first_y + count - 1) / max(height - 1, 1),
                count,
                device=device,
            )
            yy, xx = torch.meshgrid(ys, xs, indexing="ij")
            points = torch.stack((xx, yy), dim=-1)
            values = model(climate_features(points, seed))
            result[first_y : first_y + count] = values.cpu().numpy()
    model.train()
    return result


def save_preview(target: torch.Tensor, prediction: np.ndarray, path: Path) -> None:
    target_np = target.detach().cpu().numpy()
    reference_rg = np.zeros((*target_np.shape[:2], 3), dtype=np.uint8)
    reference_rg[..., 0] = np.clip(target_np[..., 1] * 255.0, 0, 255).astype(np.uint8)
    reference_rg[..., 1] = np.clip(target_np[..., 0] * 255.0, 0, 255).astype(np.uint8)

    predicted_rg = np.zeros((*prediction.shape[:2], 3), dtype=np.uint8)
    predicted_rg[..., 0] = np.clip(prediction[..., 1] * 255.0, 0, 255).astype(np.uint8)
    predicted_rg[..., 1] = np.clip(prediction[..., 0] * 255.0, 0, 255).astype(np.uint8)

    error = np.abs(prediction - target_np).mean(axis=-1)
    error_rgb = np.repeat(
        np.clip(error[..., None] * 510.0, 0, 255).astype(np.uint8), 3, axis=-1
    )
    canvas = np.concatenate((reference_rg, predicted_rg, error_rgb), axis=1)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(canvas, mode="RGB").save(path)
    Image.fromarray(predicted_rg, mode="RGB").save(
        path.with_name(path.stem + "_prediction_rg.png")
    )


def flatten(values: torch.Tensor) -> list[float]:
    return values.detach().cpu().contiguous().view(-1).tolist()


def format_float(value: float) -> str:
    if not math.isfinite(value):
        raise ValueError("Cannot export a non-finite model weight")
    if abs(value) < 5.0e-10:
        value = 0.0
    return f"{value:.9e}"


def glsl_array(name: str, values: Iterable[float]) -> str:
    data = list(values)
    lines = []
    for index in range(0, len(data), 6):
        lines.append("    " + ", ".join(format_float(v) for v in data[index:index + 6]))
    return (
        f"const float {name}[{len(data)}] = float[{len(data)}](\n"
        + ",\n".join(lines)
        + "\n);\n"
    )


def export_glsl(
    model: ClimateNetwork,
    output: Path,
    feature_seed: int,
    source_image: Path,
) -> None:
    width = model.hidden_width
    text = [
        "// Generated by Tools/ClimateNeural/train_climate.py. Do not edit weights manually.\n",
        f"// Source image: {source_image.as_posix()}\n",
        f"// Feature seed used during training: {feature_seed}\n",
        "#ifndef ENDURANCE_NEURAL_CLIMATE_WEIGHTS_GLSL\n",
        "#define ENDURANCE_NEURAL_CLIMATE_WEIGHTS_GLSL\n\n",
        "#define NEURAL_CLIMATE_MODEL_READY 1\n",
        f"#define NEURAL_CLIMATE_INPUT_COUNT {FEATURE_COUNT}\n",
        f"#define NEURAL_CLIMATE_HIDDEN0_COUNT {width}\n",
        f"#define NEURAL_CLIMATE_HIDDEN1_COUNT {width}\n",
        "#define NEURAL_CLIMATE_OUTPUT_COUNT 2\n\n",
        glsl_array("NEURAL_CLIMATE_W0", flatten(model.layer0.weight)),
        glsl_array("NEURAL_CLIMATE_B0", flatten(model.layer0.bias)),
        glsl_array("NEURAL_CLIMATE_W1", flatten(model.layer1.weight)),
        glsl_array("NEURAL_CLIMATE_B1", flatten(model.layer1.bias)),
        glsl_array("NEURAL_CLIMATE_W2", flatten(model.output.weight)),
        glsl_array("NEURAL_CLIMATE_B2", flatten(model.output.bias)),
        "\n#endif\n",
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text("".join(text), encoding="utf-8")
    temporary.replace(output)


def save_checkpoint(
    path: Path,
    model: ClimateNetwork,
    optimizer: torch.optim.Optimizer,
    step: int,
    config: TrainingConfig,
    losses: dict[str, float],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "model": model.state_dict(),
            "optimizer": optimizer.state_dict(),
            "step": step,
            "hidden_width": model.hidden_width,
            "config": asdict(config),
            "losses": losses,
        },
        path,
    )


def resolve_device(requested: str) -> torch.device:
    if requested == "auto":
        return torch.device("cuda" if torch.cuda.is_available() else "cpu")
    device = torch.device(requested)
    if device.type == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but is not available")
    return device


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Train and export the ENDURANCE Neural Climate model."
    )
    parser.add_argument("--image", required=True, type=Path, help="2048x2048 RG reference PNG")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("Asset/Shader/Noise/neural_climate_weights.glsl"),
        help="Generated GLSL include used by the Climate Noise Lab",
    )
    parser.add_argument(
        "--checkpoint",
        type=Path,
        default=Path("Tools/ClimateNeural/output/neural_climate.pt"),
    )
    parser.add_argument(
        "--preview",
        type=Path,
        default=Path("Tools/ClimateNeural/output/neural_climate_preview.png"),
    )
    parser.add_argument("--resume", type=Path)
    parser.add_argument("--steps", type=int, default=8000)
    parser.add_argument("--crop-size", type=int, default=512)
    parser.add_argument("--grid-size", type=int, default=128)
    parser.add_argument("--hidden-width", type=int, default=32)
    parser.add_argument("--learning-rate", type=float, default=0.002)
    parser.add_argument("--edge-weight", type=float, default=0.35)
    parser.add_argument("--flat-weight", type=float, default=0.15)
    parser.add_argument("--flat-threshold", type=float, default=0.012)
    parser.add_argument("--feature-seed", type=int, default=1337)
    parser.add_argument("--random-seed", type=int, default=12345)
    parser.add_argument("--preview-every", type=int, default=500)
    parser.add_argument("--device", default="auto", help="auto, cpu, cuda, cuda:0, ...")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.steps < 1:
        raise ValueError("--steps must be positive")
    if arguments.grid_size < 8:
        raise ValueError("--grid-size must be at least 8")
    if arguments.crop_size < arguments.grid_size:
        raise ValueError("--crop-size must be >= --grid-size")

    device = resolve_device(arguments.device)
    random_generator = random.Random(arguments.random_seed)
    torch.manual_seed(arguments.random_seed)
    if device.type == "cuda":
        torch.cuda.manual_seed_all(arguments.random_seed)

    target = load_target(arguments.image, device)
    height, width, _ = target.shape
    print(f"Reference: {width}x{height}; device: {device}")

    model = ClimateNetwork(arguments.hidden_width).to(device)
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=arguments.learning_rate, weight_decay=1.0e-6
    )
    start_step = 0
    if arguments.resume:
        checkpoint = torch.load(arguments.resume, map_location=device)
        checkpoint_width = int(checkpoint.get("hidden_width", arguments.hidden_width))
        if checkpoint_width != arguments.hidden_width:
            raise ValueError(
                f"Checkpoint hidden width is {checkpoint_width}, "
                f"but --hidden-width is {arguments.hidden_width}"
            )
        model.load_state_dict(checkpoint["model"])
        if "optimizer" in checkpoint:
            optimizer.load_state_dict(checkpoint["optimizer"])
        start_step = int(checkpoint.get("step", 0))
        print(f"Resuming at step {start_step}")

    config = TrainingConfig(
        image=str(arguments.image), output=str(arguments.output),
        checkpoint=str(arguments.checkpoint), preview=str(arguments.preview),
        steps=arguments.steps, crop_size=arguments.crop_size,
        grid_size=arguments.grid_size, hidden_width=arguments.hidden_width,
        learning_rate=arguments.learning_rate, edge_weight=arguments.edge_weight,
        flat_weight=arguments.flat_weight, flat_threshold=arguments.flat_threshold,
        feature_seed=arguments.feature_seed, random_seed=arguments.random_seed,
        preview_every=arguments.preview_every, device=str(device),
    )

    started = time.perf_counter()
    losses: dict[str, float] = {}
    for step in range(start_step + 1, arguments.steps + 1):
        coordinates, expected, offset = make_crop(
            target, arguments.crop_size, arguments.grid_size, random_generator
        )
        features = climate_features(coordinates, arguments.feature_seed)
        prediction = model(features)
        pixel_loss = F.smooth_l1_loss(prediction, expected, beta=0.02)
        edge_loss, flat_loss = gradient_losses(
            prediction, expected, arguments.flat_threshold
        )
        loss = pixel_loss + arguments.edge_weight * edge_loss + arguments.flat_weight * flat_loss
        optimizer.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 2.0)
        optimizer.step()

        losses = {
            "total": float(loss.detach()),
            "pixel": float(pixel_loss.detach()),
            "edge": float(edge_loss.detach()),
            "flat": float(flat_loss.detach()),
        }
        if step == 1 or step % 50 == 0:
            elapsed = time.perf_counter() - started
            print(
                f"{step:6d}/{arguments.steps} loss={losses['total']:.6f} "
                f"pixel={losses['pixel']:.6f} edge={losses['edge']:.6f} "
                f"flat={losses['flat']:.6f} crop={offset} elapsed={elapsed:.1f}s",
                flush=True,
            )

        should_preview = step == arguments.steps or (
            arguments.preview_every > 0 and step % arguments.preview_every == 0
        )
        if should_preview:
            prediction_image = evaluate_image(
                model, width, height, arguments.feature_seed, device
            )
            save_preview(target, prediction_image, arguments.preview)
            export_glsl(model, arguments.output, arguments.feature_seed, arguments.image)
            save_checkpoint(
                arguments.checkpoint, model, optimizer, step, config, losses
            )
            metadata = arguments.checkpoint.with_suffix(".json")
            metadata.write_text(
                json.dumps(
                    {"step": step, "width": width, "height": height,
                     "losses": losses, "config": asdict(config)},
                    indent=2,
                ),
                encoding="utf-8",
            )
            print(
                f"Exported GLSL: {arguments.output}\n"
                f"Preview: {arguments.preview}\n"
                f"Checkpoint: {arguments.checkpoint}",
                flush=True,
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
