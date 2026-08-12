"""Supportų parametrai.

Reikšmės — iš TIKRO profilio (`prusa-full.ini`, TinyMaker + „Universal 0.05 -
Light Supports"), o ne iš numatytųjų ir ne iš atminties. Vardai palikti tokie
patys kaip PrusaSlicer'io nustatymuose, kad matytųsi, iš kur kiekvienas skaičius.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field


def default_support_curve() -> list[tuple[float, float]]:
    """`create_default_support_curve()` (SupportPointGenerator.cpp:1453).

    (x, y) = (didžiausias atstumas sluoksnyje XY, aukščio skirtumas Z), mm.
    Skaityti taip: ką tik pastatyta atrama „dengia" 3,2 mm spindulį; kylant
    aukštyn tas spindulys auga iki 6 mm ties 40 mm. Būtent tai, o ne pastovus
    žingsnis, ir valdo atramų tankį.
    """
    return [(3.2, 0.0), (4.0, 3.9), (5.0, 15.0), (6.0, 40.0)]


@dataclass
class SupportConfig:
    # --- SupportTreeConfig (DefaultSupportTree) ---
    head_front_radius_mm: float = 0.25      # support_head_front_diameter 0.5
    head_back_radius_mm: float = 0.5        # support_pillar_diameter 1
    head_fallback_radius_mm: float = 0.3    # support_small_pillar_diameter_percent 60%
    head_penetration_mm: float = 0.3        # support_head_penetration
    head_width_mm: float = 3.0              # support_head_width
    pillar_radius_mm: float = 0.5           # support_pillar_diameter 1
    base_radius_mm: float = 1.5             # support_base_diameter 3
    base_height_mm: float = 1.0             # support_base_height
    safety_distance_mm: float = 1.0         # support_base_safety_distance
    max_bridge_length_mm: float = 10.0      # support_max_bridge_length
    max_pillar_link_distance_mm: float = 10.0   # support_max_pillar_link_distance
    max_bridges_on_pillar: int = 3          # support_max_bridges_on_pillar
    bridge_slope: float = math.pi / 4       # 45 laipsniai
    critical_angle: float = math.pi / 4     # support_critical_angle 45
    #: `normal_cutoff_angle` (SupportTreeConfig) - per status polinkis
    #: atmetamas: polar < PI - normal_cutoff_angle (cpp:441).
    normal_cutoff_angle: float = math.pi / 2
    ground_facing_only: bool = False        # support_buildplate_only 0
    #: support_object_elevation = 5, BET pad_around_object = 1 -> nekeliam.
    object_elevation_mm: float = 0.0
    pillar_cascade_neighbors: int = 3

    # --- SupportPointGeneratorConfig ---
    density_relative: float = 1.0           # support_points_density_relative 100 %
    #: PrepareSupportConfig::discretize_overhang_step (SampleConfig.hpp:18)
    discretize_overhang_step_mm: float = 2.0
    #: PrepareSupportConfig::minimal_bounding_sphere_radius (SampleConfig.hpp:35) -
    #: mazesnes dalys ismetamos kaip neatspausdinamos.
    minimal_part_radius_mm: float = 0.2
    support_curve: list[tuple[float, float]] = field(default_factory=default_support_curve)

    # --- Pad (SLA/Pad.hpp + profilis) ---
    pad_thickness_mm: float = 0.15          # pad_wall_thickness 0.15, wall_height 0
    pad_brim_mm: float = 1.6                # pad_brim_size

    def influence_radius(self, dz: float) -> float:
        """Atramos taško įtakos spindulys, kai esam `dz` mm virš jo.

        `prepare_supports_for_layer` (SPG.cpp:495-543): tiesinė interpoliacija
        kreivėje, o density mažina spindulį per sqrt(r² / density).
        """
        curve = self.support_curve
        if dz <= curve[0][1]:
            r = curve[0][0]
        elif dz >= curve[-1][1]:
            r = curve[-1][0]
        else:
            r = curve[-1][0]
            for (x0, y0), (x1, y1) in zip(curve, curve[1:]):
                if y0 <= dz <= y1:
                    t = (dz - y0) / (y1 - y0) if y1 > y0 else 0.0
                    r = x0 + t * (x1 - x0)
                    break
        if abs(self.density_relative - 1.0) > 1e-4:
            r = math.sqrt(r * r / self.density_relative)
        return r
