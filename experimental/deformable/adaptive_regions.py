"""Photo-space regional lattice definitions and smooth local support."""
import numpy as np


def validate_regions(regions, shape=None):
    if not isinstance(regions, list) or len(regions) > 64:
        raise ValueError("Expected at most 64 adaptive regions")
    for region in regions:
        rect = np.asarray(region["rect"], float)
        if rect.shape != (4,) or not np.isfinite(rect).all() or min(rect[:2]) < 0 or min(rect[2:]) < 8:
            raise ValueError("Regions require a finite photo rectangle x,y,width,height (minimum 8 px)")
        if shape and (rect[0] + rect[2] > shape[1] or rect[1] + rect[3] > shape[0]):
            raise ValueError("Adaptive region exceeds photograph bounds")
        if region["mode"] not in {"refine", "protect"}:
            raise ValueError("Region mode must be refine or protect")
        if region["mode"] == "refine" and not 3 <= int(region.get("nodes", 9)) <= 33:
            raise ValueError("Regional lattice requires 3..33 nodes per axis")
    return regions


def photo_grid(roi, shape, device):
    import torch
    x, y, w, h = roi
    yy, xx = torch.meshgrid(torch.linspace(y, y + h - 1, shape[0], device=device),
                            torch.linspace(x, x + w - 1, shape[1], device=device), indexing="ij")
    return xx, yy


def patch_delta(nodes, rect, roi, shape, amplitude=8.):
    """Local bicubic lattice; exactly zero outside patch, C1 sine taper at edges."""
    import torch
    import torch.nn.functional as F
    xx, yy = photo_grid(roi, shape, nodes.device)
    x, y, w, h = rect
    # Clip to the common field domain so the residual still vanishes at its edges.
    left, top = max(x, roi[0]), max(y, roi[1])
    right, bottom = min(x + w - 1, roi[0] + roi[2] - 1), min(y + h - 1, roi[1] + roi[3] - 1)
    if right <= left or bottom <= top:
        return nodes.sum() * 0 + torch.zeros((1, 2, *shape), device=nodes.device)
    tx, ty = (xx - left) / (right - left), (yy - top) / (bottom - top)
    grid = torch.stack([2 * tx - 1, 2 * ty - 1], -1)[None]
    dense = F.grid_sample(nodes, grid, mode="bicubic", padding_mode="zeros", align_corners=True)
    taper = torch.sin(torch.pi * tx.clamp(0, 1)).square() * torch.sin(torch.pi * ty.clamp(0, 1)).square()
    inside = ((tx > 0) & (tx < 1) & (ty > 0) & (ty < 1)).float()
    return dense.tanh() * (amplitude / np.sqrt(2)) * taper * inside


def protection_weight(regions, roi, shape, device):
    import torch
    xx, yy = photo_grid(roi, shape, device)
    weight = torch.ones(shape, device=device)
    for region in regions:
        if region["mode"] != "protect":
            continue
        x, y, w, h = region["rect"]
        dx = torch.maximum((x - xx).clamp_min(0), (xx - (x + w - 1)).clamp_min(0))
        dy = torch.maximum((y - yy).clamp_min(0), (yy - (y + h - 1)).clamp_min(0))
        t = (torch.sqrt(dx.square() + dy.square()) / 16.).clamp(0, 1)
        weight = weight * t.square() * (3 - 2 * t)
    return weight[None, None]


def adaptive_delta(patches, regions, roi, shape, batch_size, device):
    import torch
    per_slice = []
    for index in range(batch_size):
        delta = torch.zeros((1, 2, *shape), device=device)
        for region, nodes in patches:
            if region["sequence_index"] == index:
                delta = delta + patch_delta(nodes, region["rect"], roi, shape)
        local = [r for r in regions if r["sequence_index"] == index]
        per_slice.append(delta * protection_weight(local, roi, shape, device))
    return torch.cat(per_slice)
