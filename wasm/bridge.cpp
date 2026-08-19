/*
 * Tiltas tarp JS ir libslic3r SLA grandines.
 *
 * Kviecia TA PACIA seka, kaip SLAPrint: pjaustymas -> sejos paruosimas ->
 * tasku generavimas -> medis -> padas.
 *
 * Du iejimai i ta pati darba:
 *   - `sla_slice(kelias, sluoksnis, medis)` - is JS (narsykle ar node), grazina
 *     JSON eilute. Modelis paduodamas per Emscripten failu sistema: JS ji irašo
 *     i `/model.stl` ir paduoda ta keli.
 *   - `main()` - komandinei eilutei, kad ta pati grandine butu galima paleisti
 *     be narsykles: `node sla.js modelis.stl 0.05 [tree]`
 *
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - libslic3r
 * Copyright (C) 2026 Viktoras Sidlauskas - sis tiltas
 * AGPL-3.0-or-later (isvestinis is libslic3r kurinys).
 */
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <string>

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>
#include <libslic3r/AABBMesh.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>
#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/SLA/JobController.hpp>
#include <libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

using namespace Slic3r;
using Clock = std::chrono::steady_clock;

static long ms_since(Clock::time_point t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

/*
 * Medzio parametrai - pazodziui pagal `make_support_cfg` (SLAPrint.cpp:50-115),
 * sustatyti su V profilio reiksmemis is `slicer-lab/prusa-full.ini`.
 *
 * Kiekvienas tipas turi SAVA parametru rinkini, ne ta pati kitu vardu.
 *
 * ⚠️ `pad_around_object = 1` reiskia „zero elevation", tad object_elevation_mm
 * yra 0, nors profilyje irasyta 5. Su 5 visos atramos pakiltu 5 mm i ora.
 */
static sla::SupportTreeConfig make_tree_cfg(bool branching)
{
    sla::SupportTreeConfig scfg;
    scfg.enabled = true;
    scfg.object_elevation_mm = 0.0;
    scfg.bridge_slope = 45.0 * M_PI / 180.0;        // (branching)support_critical_angle 45
    scfg.ground_facing_only = false;                // support_buildplate_only 0
    scfg.base_height_mm = 1.0;
    scfg.pillar_base_safety_distance_mm = 1.0;      // support_base_safety_distance 1
    scfg.max_weight_on_model_support = 10.0;
    scfg.max_pillar_link_distance_mm = 10.0;

    const double pillar_r = 0.5 * 1.0;              // abiem tipams pillar_diameter 1

    if (!branching) {
        scfg.tree_type = sla::SupportTreeType::Default;
        scfg.head_front_radius_mm = 0.5 * 0.5;      // support_head_front_diameter 0,5
        scfg.head_back_radius_mm = pillar_r;
        scfg.head_fallback_radius_mm = 0.01 * 60.0 * pillar_r;  // small_pillar 60 %
        scfg.head_penetration_mm = 0.3;
        scfg.head_width_mm = 3.0;
        scfg.max_bridge_length_mm = 10.0;
        scfg.pillar_connection_mode = sla::PillarConnectionMode::zigzag;
        scfg.pillar_widening_factor = 0.0;
        scfg.base_radius_mm = 0.5 * 3.0;            // support_base_diameter 3
        scfg.max_bridges_on_pillar = 3;
    } else {
        scfg.tree_type = sla::SupportTreeType::Branching;
        scfg.head_front_radius_mm = 0.5 * 0.4;      // branchingsupport_head_front_diameter
        scfg.head_back_radius_mm = pillar_r;
        scfg.head_fallback_radius_mm = 0.01 * 50.0 * pillar_r;  // small_pillar 50 %
        scfg.head_penetration_mm = 0.2;
        scfg.head_width_mm = 1.0;
        scfg.max_bridge_length_mm = 5.0;
        scfg.pillar_connection_mode = sla::PillarConnectionMode::dynamic;
        scfg.pillar_widening_factor = 0.5;
        scfg.base_radius_mm = 0.5 * 4.0;            // branchingsupport_base_diameter 4
        scfg.max_bridges_on_pillar = 2;
    }
    return scfg;
}

/** `make_pad_cfg` (SLAPrint.cpp:136-149) su V profilio reiksmemis. */
static sla::PadConfig make_pad_config()
{
    sla::PadConfig pcfg;
    pcfg.wall_thickness_mm = 0.15;                  // pad_wall_thickness
    pcfg.wall_slope = 90.0 * M_PI / 180.0;          // pad_wall_slope
    pcfg.max_merge_dist_mm = 50.0;                  // pad_max_merge_distance
    pcfg.wall_height_mm = 0.0;                      // pad_wall_height
    pcfg.brim_size_mm = 1.6;                        // pad_brim_size
    pcfg.embed_object.enabled = true;               // pad_around_object 1
    pcfg.embed_object.everywhere = false;           // pad_around_object_everywhere 0
    pcfg.embed_object.object_gap_mm = 1.0;          // pad_object_gap
    pcfg.embed_object.stick_width_mm = 0.5;         // pad_object_connector_width
    pcfg.embed_object.stick_stride_mm = 10.0;       // pad_object_connector_stride
    pcfg.embed_object.stick_penetration_mm = 0.3;   // pad_object_connector_penetration
    return pcfg;
}

/* Rezultatas laikomas cia, kad JS pusei uztektu grazinti rodykle. */
static std::string g_json;

/**
 * Visa grandine. `path` - failas Emscripten failu sistemoje (arba tikras
 * failas, kai kompiliuojama ne i WASM).
 */
static const char *run_chain(const char *path, double layer_h, bool branching, bool verbose)
{
    char buf[2048];
    TriangleMesh mesh;
    if (!mesh.ReadSTLFile(path)) {
        g_json = "{\"klaida\":\"STL neperskaitytas\"}";
        return g_json.c_str();
    }
    const indexed_triangle_set &its = mesh.its;
    if (verbose) {
        std::printf("model            %s\n", path);
        std::printf("trikampiu        %zu\n", its.indices.size());
        std::printf("atramu tipas     %s\n", branching ? "tree (branching)" : "regular (default)");
    }

    const auto bb = mesh.bounding_box();
    std::vector<float> grid;
    for (float z = float(bb.min.z()) + float(layer_h) / 2.f; z < float(bb.max.z()); z += float(layer_h))
        grid.push_back(z);
    if (verbose) std::printf("sluoksniu        %zu\n", grid.size());

    auto t0 = Clock::now();
    std::vector<ExPolygons> slices = slice_mesh_ex(its, grid, 0.005f);   // slice_closing_radius
    const long t_slice = ms_since(t0);

    t0 = Clock::now();
    sla::PrepareSupportConfig prep_cfg;
    sla::SupportPointGeneratorData data =
        sla::prepare_generator_data(std::move(slices), grid, prep_cfg);
    const long t_prep = ms_since(t0);

    /* Sejos parametrai - SLAPrintSteps.cpp:715-733 (density_relative = 100 %). */
    t0 = Clock::now();
    sla::SupportPointGeneratorConfig gen_cfg;
    gen_cfg.density_relative = 1.f;
    gen_cfg.head_diameter = branching ? 0.4f : 0.5f;
    gen_cfg.island_configuration = sla::SampleConfigFactory::apply_density(
        sla::SampleConfigFactory::create(gen_cfg.head_diameter), gen_cfg.density_relative);
    sla::LayerSupportPoints layer_pts = sla::generate_support_points(data, gen_cfg);
    const long t_pts = ms_since(t0);
    if (verbose) std::printf("tasku            %zu\n", layer_pts.size());

    AABBMesh emesh{its};
    sla::SupportPoints pts = sla::move_on_mesh_surface(layer_pts, emesh, 1.0);

    sla::SupportableMesh sm{its, pts, make_tree_cfg(branching)};
    sm.pad_cfg = make_pad_config();

    t0 = Clock::now();
    sla::JobController ctl;
    auto tree = sla::create_support_tree(sm, ctl);
    const long t_tree = ms_since(t0);
    if (verbose) std::printf("atramu trikampiu %zu\n", tree.first.indices.size());

    /* ⚠️ NEISTIRTA (2026-08-19): su „tree" padas iseina TUSCIAS (0 mm3), o
       PrusaSlicer tam paciam puodeliui pirmame sluoksnyje turi ~247 mm2
       pedsaka. Bendra derva sutampa per 1,6 %, tad efektas mazas, bet
       priezastis nezinoma - tikrinti pries siulant „tree" naudotojui. */
    t0 = Clock::now();
    indexed_triangle_set pad = sla::create_pad(sm, tree.first, ctl);
    const long t_pad = ms_since(t0);
    if (verbose) std::printf("pado trikampiu   %zu\n", pad.indices.size());

    const float v_model = its_volume(its);
    const float v_sup   = its_volume(tree.first);
    const float v_pad   = its_volume(pad);
    if (verbose) {
        std::printf("turis_mm3        modelis %.1f  atramos %.1f  padas %.1f  viso %.4f ml\n",
                    v_model, v_sup, v_pad, (v_model + v_sup + v_pad) / 1000.0);
        std::printf("laikas_ms        {\"slice\":%ld,\"prepare\":%ld,\"points\":%ld,"
                    "\"tree\":%ld,\"pad\":%ld,\"viso\":%ld}\n",
                    t_slice, t_prep, t_pts, t_tree, t_pad,
                    t_slice + t_prep + t_pts + t_tree + t_pad);
    }

    /* Geometrija atiduodama i modulio failu sistema, kad JS puse galetu ja
       nupiesti. Trys atskiri failai, nes piesiant kiekvienas turi savo spalva. */
    its_write_stl_binary("/out_model.stl", "modelis", its);
    its_write_stl_binary("/out_supports.stl", "atramos", tree.first);
    its_write_stl_binary("/out_pad.stl", "padas", pad);

    std::snprintf(buf, sizeof(buf),
        "{\"tipas\":\"%s\",\"trikampiu\":%zu,\"sluoksniu\":%zu,\"tasku\":%zu,"
        "\"atramu_trikampiu\":%zu,\"pado_trikampiu\":%zu,"
        "\"turis\":{\"modelis\":%.1f,\"atramos\":%.1f,\"padas\":%.1f,\"viso_ml\":%.4f},"
        "\"laikas_ms\":{\"slice\":%ld,\"prepare\":%ld,\"points\":%ld,\"tree\":%ld,"
        "\"pad\":%ld,\"viso\":%ld}}",
        branching ? "tree" : "regular",
        its.indices.size(), grid.size(), layer_pts.size(),
        tree.first.indices.size(), pad.indices.size(),
        v_model, v_sup, v_pad, (v_model + v_sup + v_pad) / 1000.0,
        t_slice, t_prep, t_pts, t_tree, t_pad,
        t_slice + t_prep + t_pts + t_tree + t_pad);
    g_json = buf;
    return g_json.c_str();
}

extern "C" {

/**
 * Iejimas is JS. `path` - failas modulio failu sistemoje (`FS.writeFile`).
 * Grazina JSON eilute; ji galioja iki kito kvietimo.
 */
EMSCRIPTEN_KEEPALIVE
const char *sla_slice(const char *path, double layer_h, int branching)
{
    return run_chain(path, layer_h, branching != 0, false);
}

} // extern "C"

int main(int argc, char **argv)
{
    if (argc < 2) { std::printf("naudojimas: sla.js <model.stl> [sluoksnis] [tree]\n"); return 2; }
    const double layer_h = argc > 2 ? std::atof(argv[2]) : 0.05;
    const std::string t  = argc > 3 ? argv[3] : "regular";
    run_chain(argv[1], layer_h, t == "tree" || t == "branching", true);
    return 0;
}
