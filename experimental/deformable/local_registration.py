"""Regularized regional inverse deformation on a shared photo grid, coupled in Z."""
from dataclasses import dataclass, asdict

import cv2
import numpy as np
import torch
import torch.nn.functional as F
from deformation import jacobian, native_maps, remap_native
from adaptive_regions import adaptive_delta, validate_regions


@dataclass
class LocalSettings:
    working_size: int = 256
    nodes: tuple = (7, 13)
    iterations: int = 120
    max_displacement_px: float = 32.
    body_weight: float = 1.
    bone_weight: float = 1.
    magnitude_weight: float = .025
    smoothness_weight: float = .3
    rigidity_weight: float = .7
    continuity_weight: float = .15
    minimum_jacobian: float = .3
    max_gap_mm: float = 2.
    device: str = "cpu"


def signed_distance(mask):
    value = (mask != 0).astype(np.uint8)
    return cv2.distanceTransform(1 - value, cv2.DIST_L2, cv2.DIST_MASK_PRECISE) - cv2.distanceTransform(value, cv2.DIST_L2, cv2.DIST_MASK_PRECISE)


def field_at_resolution(nodes, shape, max_displacement):
    """Smooth bicubic control lattice, bounded norm, zero derivative at boundary."""
    dense = F.interpolate(nodes, size=shape, mode="bicubic", align_corners=True)
    y = torch.linspace(0, torch.pi, shape[0], device=nodes.device)
    x = torch.linspace(0, torch.pi, shape[1], device=nodes.device)
    taper = y.sin().square()[:, None] * x.sin().square()[None, :]
    return dense.tanh() * (max_displacement / np.sqrt(2)) * taper


def dense_field(nodes, shape, max_displacement):
    value = torch.as_tensor(nodes, dtype=torch.float32).unsqueeze(0)
    with torch.no_grad():
        return field_at_resolution(value, shape, max_displacement)[0].permute(1, 2, 0).numpy()


def fitted_field(fit, index, shape, stage=None):
    stage = stage or {"nodes": fit["nodes"], "patches": fit.get("patches", [])}
    base = dense_field(stage["nodes"][index], shape, fit["settings"]["max_displacement_px"])
    if stage.get("patches"):
        patches = [({**p["region"], "sequence_index": 0}, torch.as_tensor(p["nodes"]))
                   for p in stage["patches"] if p["region"]["sequence_index"] == index]
        regions = [{**r, "sequence_index": 0} for r in fit.get("regions", []) if r["sequence_index"] == index]
        with torch.no_grad():
            delta = adaptive_delta(patches, regions, fit["roi"], shape, 1, "cpu")
        base = base + delta[0].permute(1, 2, 0).numpy()
    return base


def sequence_links(geometries, transforms, max_gap_mm):
    """Compare residual vectors in patient mm, at matching aligned-photo XY."""
    matrices, links = [], []
    for geometry, transform in zip(geometries, transforms):
        basis = np.asarray(geometry["native_pixel_xy_to_patient_mm_4x4"], float)
        matrices.append(basis[:3, :2] @ np.linalg.inv(transform)[:2, :2])
    for i in range(len(geometries) - 1):
        a, b = geometries[i], geometries[i + 1]
        oa, ob = np.asarray(a["orientation_patient"]), np.asarray(b["orientation_patient"])
        na, nb = np.cross(oa[:3], oa[3:]), np.cross(ob[:3], ob[3:])
        if np.dot(na, nb) < .999 or a["series_uid"] != b["series_uid"]:
            continue
        if a.get("frame_of_reference_uid") and b.get("frame_of_reference_uid") and a["frame_of_reference_uid"] != b["frame_of_reference_uid"]:
            continue
        dz = abs(float(np.dot(np.asarray(b["position_patient_mm"]) - a["position_patient_mm"], na)))
        if .01 <= dz <= max_gap_mm:
            links.append((i, i + 1, dz))
    return np.asarray(matrices, np.float32), links


def fit_sequence(fixed_bodies, moving_bodies, fixed_bones, moving_bones, supports,
                 geometries, transforms, settings=None, callback=None, cancel_check=None, regions=None):
    cfg = settings or LocalSettings()
    if cfg.max_displacement_px <= 0 or cfg.working_size < 64 or cfg.iterations < 1 or not cfg.nodes or min(cfg.nodes) < 3:
        raise ValueError("Invalid local registration parameters")
    lists = [fixed_bodies, moving_bodies, fixed_bones, moving_bones, supports, geometries, transforms]
    if not fixed_bodies or any(len(values) != len(fixed_bodies) for values in lists):
        raise ValueError("Sequence inputs must have the same nonzero length")
    shape = fixed_bodies[0].shape
    regions = validate_regions(regions or [], shape)
    if any(not 0 <= r["sequence_index"] < len(fixed_bodies) for r in regions):
        raise ValueError("Adaptive region has invalid sequence index")
    for masks in lists[:5]:
        if any(value.shape != shape for value in masks):
            raise ValueError("A common aligned photograph grid is required")
    for i in range(len(fixed_bodies)):
        if not np.any(fixed_bones[i] & supports[i]) or not np.any(moving_bones[i]):
            raise ValueError(f"Bone guidance is unavailable at sequence index {i}")
    union = np.logical_or.reduce([mask != 0 for mask in fixed_bodies + moving_bodies])
    y, x = np.nonzero(union)
    margin = int(np.ceil(cfg.max_displacement_px)) + 20
    left, top = max(0, int(x.min()) - margin), max(0, int(y.min()) - margin)
    right, bottom = min(shape[1], int(x.max()) + margin + 1), min(shape[0], int(y.max()) + margin + 1)
    width, height = right - left, bottom - top
    roi = [left, top, width, height]
    for region in regions:
        if region["mode"] == "refine":
            x, y, w, h = region["rect"]
            pw, ph = min(x + w - 1, right - 1) - max(x, left), min(y + h - 1, bottom - 1) - max(y, top)
            count = int(region.get("nodes", 9))
            if pw <= 0 or ph <= 0:
                raise ValueError("Refinement region is outside the body field ROI")
            if pw / (count - 1) >= (width - 1) / (cfg.nodes[-1] - 1) or ph / (count - 1) >= (height - 1) / (cfg.nodes[-1] - 1):
                raise ValueError("Regional lattice must be finer than the general lattice: increase regional nodes or draw a smaller region")
    factor = min(1., cfg.working_size / max(width, height))
    wh = (max(16, round(width * factor)), max(16, round(height * factor)))
    work_shape = (wh[1], wh[0])
    pixel_step = np.array([(width - 1) / (wh[0] - 1), (height - 1) / (wh[1] - 1)], np.float32)
    for region in regions:
        if region["mode"] == "refine":
            _, _, rw, rh = region["rect"]
            if min((rw - 1) / pixel_step[0], (rh - 1) / pixel_step[1]) < int(region.get("nodes", 9)) - 1:
                raise ValueError("Processing resolution is too low for the regional lattice; increase working-size")
    device = torch.device(cfg.device)
    torch.set_num_threads(min(4, torch.get_num_threads()))
    def tensor(value):
        return torch.as_tensor(value, dtype=torch.float32, device=device)
    def small(mask):
        return cv2.resize(mask[top:bottom, left:right].astype(np.uint8), wh, interpolation=cv2.INTER_NEAREST)
    def sdf_batch(body, bone):
        return np.stack([np.stack([np.clip(signed_distance(small(a)), -12, 12),
                                   np.clip(signed_distance(small(b)), -12, 12)]) for a, b in zip(body, bone)])
    target_sdf = tensor(sdf_batch(fixed_bodies, fixed_bones))
    source_sdf = tensor(sdf_batch(moving_bodies, moving_bones))
    target_probability = torch.sigmoid(-target_sdf / .7)
    annotation = tensor(np.stack([small(mask) != 0 for mask in supports]))[:, None]
    known = torch.cat([torch.ones_like(annotation), annotation], dim=1)
    weights = tensor([cfg.body_weight, cfg.bone_weight])[None, :, None, None] * known
    ygrid, xgrid = torch.meshgrid(torch.linspace(-1, 1, wh[1], device=device), torch.linspace(-1, 1, wh[0], device=device), indexing="ij")
    base = torch.stack([xgrid, ygrid], dim=-1)[None]
    conversion = tensor([2 / (width - 1), 2 / (height - 1)])[None, :, None, None]
    physical_matrices, links = sequence_links(geometries, transforms, cfg.max_gap_mm)
    physical = tensor(physical_matrices)
    trace = []

    def losses(field):
        grid = base + (field * conversion).permute(0, 2, 3, 1)
        warped_sdf = F.grid_sample(source_sdf, grid, mode="bilinear", padding_mode="border", align_corners=True)
        predicted = torch.sigmoid(-warped_sdf / .7)
        # Squared-Dice is stationary at identical soft masks, unlike linear soft-Dice.
        intersection = (known * predicted * target_probability).sum((2, 3))
        denominator = (known * (predicted.square() + target_probability.square())).sum((2, 3))
        dloss = (1 - (2 * intersection + 1e-5) / (denominator + 1e-5))
        data = (dloss * tensor([cfg.body_weight, cfg.bone_weight])[None]).mean()
        surface = (weights * (warped_sdf - target_sdf).abs()).sum() / weights.sum().clamp_min(1) / 12
        dx = (field[:, :, :, 1:] - field[:, :, :, :-1]) / pixel_step[0]
        dy = (field[:, :, 1:, :] - field[:, :, :-1, :]) / pixel_step[1]
        smooth = dx.square().mean() + dy.square().mean()
        magnitude = (field / cfg.max_displacement_px).square().mean()
        bone = target_probability[:, 1:2]
        rigidity = (dx.square() * bone[:, :, :, :-1]).sum() / bone[:, :, :, :-1].sum().clamp_min(1)
        rigidity += (dy.square() * bone[:, :, :-1, :]).sum() / bone[:, :, :-1, :].sum().clamp_min(1)
        j = (1 + dx[:, 0, :-1]) * (1 + dy[:, 1, :, :-1]) - dx[:, 1, :-1] * dy[:, 0, :, :-1]
        folding = torch.relu(cfg.minimum_jacobian + .15 - j).square().mean()
        continuity = field.sum() * 0
        if links:
            vectors = torch.einsum("nki,nihw->nkhw", physical, field)
            continuity = torch.stack([((vectors[a] - vectors[b]) / dz).square().mean() for a, b, dz in links]).mean()
        total = data + .2 * surface + cfg.magnitude_weight * magnitude + cfg.smoothness_weight * smooth
        total += cfg.rigidity_weight * rigidity + 30 * folding + cfg.continuity_weight * continuity
        return total, data, j.min()

    previous = None
    stages = []
    for level, count in enumerate(cfg.nodes):
        initial = torch.zeros((len(fixed_bodies), 2, count, count), device=device) if previous is None else F.interpolate(previous, (count, count), mode="bicubic", align_corners=True)
        nodes = torch.nn.Parameter(initial)
        optimizer = torch.optim.Adam([nodes], lr=.035)
        best_nodes, best_loss = nodes.detach().clone(), float("inf")
        for iteration in range(cfg.iterations):
            if cancel_check:
                cancel_check()
            optimizer.zero_grad()
            field = field_at_resolution(nodes, work_shape, cfg.max_displacement_px)
            total, data, minimum = losses(field)
            if not torch.isfinite(total):
                raise ValueError("Non-finite local objective")
            value = float(total.detach())
            if value < best_loss and float(minimum.detach()) >= cfg.minimum_jacobian:
                best_loss, best_nodes = value, nodes.detach().clone()
            if iteration % 20 == 0 or iteration == cfg.iterations - 1:
                record = {"level": level, "iteration": iteration, "objective": value,
                          "data_loss": float(data.detach()), "minimum_jacobian_work_grid": float(minimum.detach())}
                trace.append(record)
                if callback:
                    callback(record)
            total.backward()
            optimizer.step()
        previous = best_nodes
        stages.append({"level": level, "count": count, "nodes": previous.cpu().numpy().copy(),
                       "objective": best_loss})
    patches = []
    refinements = [r for r in regions if r["mode"] == "refine"]
    if refinements:
        parameters = [torch.nn.Parameter(torch.zeros((1, 2, int(r.get("nodes", 9)), int(r.get("nodes", 9))), device=device))
                      for r in refinements]
        frozen = field_at_resolution(previous, work_shape, cfg.max_displacement_px).detach()
        optimizer = torch.optim.Adam(parameters, lr=.035)
        best_parameters = [p.detach().clone() for p in parameters]
        best_loss = float("inf")
        for iteration in range(cfg.iterations):
            if cancel_check:
                cancel_check()
            optimizer.zero_grad()
            delta = adaptive_delta(list(zip(refinements, parameters)), regions, roi, work_shape, len(fixed_bodies), device)
            field = frozen + delta
            total, data, minimum = losses(field)
            total = total + .02 * (delta / 8.).square().mean()
            total = total + torch.relu(torch.linalg.vector_norm(field, dim=1) - cfg.max_displacement_px).square().mean()
            if not torch.isfinite(total):
                raise ValueError("Non-finite adaptive objective")
            value = float(total.detach())
            if value < best_loss and float(minimum.detach()) >= cfg.minimum_jacobian:
                best_loss = value
                best_parameters = [p.detach().clone() for p in parameters]
            if iteration % 20 == 0 or iteration == cfg.iterations - 1:
                record = {"level": len(cfg.nodes), "iteration": iteration, "objective": value,
                          "data_loss": float(data.detach()), "minimum_jacobian_work_grid": float(minimum.detach()), "adaptive": True}
                trace.append(record)
                if callback:
                    callback(record)
            total.backward()
            optimizer.step()
        patches = [{"region": r, "nodes": p.cpu().numpy()} for r, p in zip(refinements, best_parameters)]
        stages.append({"level": len(cfg.nodes), "count": cfg.nodes[-1], "nodes": previous.cpu().numpy().copy(),
                       "objective": best_loss, "patches": patches, "adaptive": True})
    return {"nodes": previous.cpu().numpy(), "roi": roi, "settings": asdict(cfg), "trace": trace,
            "stages": stages,
            "patches": patches, "regions": regions,
            "links": links, "physical_matrices": physical_matrices, "working_shape": list(work_shape)}
