/*
 * Tiltas tarp JS ir libslic3r SLA grandines.
 *
 * Kviecia TA PACIA seka, kaip SLAPrint: pjaustymas -> sejos paruosimas ->
 * tasku generavimas -> medis -> padas. Spausdina ta pacia statistika, kaip
 * musu `sla_run.mjs`, kad palyginimas butu tiesioginis.
 *
 * Paleisti (node): node bridge.js <model.stl> [sluoksnio_aukstis]
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

#include <libslic3r/I18N.hpp>

using namespace Slic3r;

using Clock = std::chrono::steady_clock;

static long ms_since(Clock::time_point t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

int main(int argc, char **argv)
{
    if (argc < 2) { std::printf("reikia STL kelio\n"); return 2; }
    const std::string path = argv[1];
    const float layer_h = argc > 2 ? float(std::atof(argv[2])) : 0.05f;
    /* Treciasis argumentas - atramu tipas: "regular" (numatytasis) arba "tree".
       PrusaSlicer profilyje tai `support_tree_type`, ir kiekvienam tipui jis
       turi ATSKIRA parametru rinkini (`support_*` ir `branchingsupport_*`). */
    const std::string tree_arg = argc > 3 ? argv[3] : "regular";
    const bool branching = (tree_arg == "tree" || tree_arg == "branching");

    TriangleMesh mesh;
    if (!mesh.ReadSTLFile(path.c_str())) { std::printf("STL neperskaitytas: %s\n", path.c_str()); return 3; }
    const indexed_triangle_set &its = mesh.its;
    std::printf("model            %s\n", path.c_str());
    std::printf("trikampiu        %zu\n", its.indices.size());
    std::printf("atramu tipas     %s\n", branching ? "tree (branching)" : "regular (default)");

    auto bb = mesh.bounding_box();
    std::vector<float> grid;
    for (float z = float(bb.min.z()) + layer_h / 2.f; z < float(bb.max.z()); z += layer_h)
        grid.push_back(z);
    std::printf("sluoksniu        %zu\n", grid.size());

    auto t0 = Clock::now();
    std::vector<ExPolygons> slices = slice_mesh_ex(its, grid, 0.005f);
    long t_slice = ms_since(t0);

    t0 = Clock::now();
    sla::PrepareSupportConfig prep_cfg;
    sla::SupportPointGeneratorData data =
        sla::prepare_generator_data(std::move(slices), grid, prep_cfg);
    long t_prep = ms_since(t0);

    t0 = Clock::now();
    /* Sejos parametrai - kaip SLAPrintSteps.cpp:715-733 su V profiliu
       (support_points_density_relative = 100, support_head_front_diameter = 0,5). */
    sla::SupportPointGeneratorConfig gen_cfg;
    gen_cfg.density_relative = 1.f;
    gen_cfg.head_diameter = branching ? 0.4f : 0.5f;   // SLAPrintSteps.cpp:718-726
    gen_cfg.island_configuration = sla::SampleConfigFactory::apply_density(
        sla::SampleConfigFactory::create(gen_cfg.head_diameter), gen_cfg.density_relative);
    sla::LayerSupportPoints layer_pts = sla::generate_support_points(data, gen_cfg);
    long t_pts = ms_since(t0);
    std::printf("tasku            %zu\n", layer_pts.size());

    AABBMesh emesh{its};
    sla::SupportPoints pts = sla::move_on_mesh_surface(layer_pts, emesh, 1.0);

    /* Medzio ir pado parametrai - pazodziui pagal `make_support_cfg` ir
       `make_pad_cfg` (SLAPrint.cpp:50-149), sustatyti su V profilio
       reiksmemis is `slicer-lab/prusa-full.ini`.
       ⚠️ `pad_around_object = 1` reiskia „zero elevation", tad
       object_elevation_mm = 0, nors profilyje irasyta 5. */
    sla::SupportTreeConfig scfg;
    scfg.enabled = true;
    if (!branching) {
        /* „regular" - `make_support_cfg` saka Default (SLAPrint.cpp:58-81),
           parametrai is V profilio `support_*`. */
        scfg.tree_type = sla::SupportTreeType::Default;
        scfg.head_front_radius_mm = 0.5 * 0.5;      // support_head_front_diameter 0,5
        const double pillar_r = 0.5 * 1.0;          // support_pillar_diameter 1
        scfg.head_back_radius_mm = pillar_r;
        scfg.head_fallback_radius_mm = 0.01 * 60.0 * pillar_r;  // small_pillar 60 %
        scfg.head_penetration_mm = 0.3;
        scfg.head_width_mm = 3.0;
        scfg.object_elevation_mm = 0.0;             // zero elevation (pad_around_object = 1)
        scfg.bridge_slope = 45.0 * M_PI / 180.0;    // support_critical_angle 45
        scfg.max_bridge_length_mm = 10.0;
        scfg.max_pillar_link_distance_mm = 10.0;
        scfg.pillar_connection_mode = sla::PillarConnectionMode::zigzag;
        scfg.ground_facing_only = false;            // support_buildplate_only 0
        scfg.pillar_widening_factor = 0.0;
        scfg.base_radius_mm = 0.5 * 3.0;            // support_base_diameter 3
        scfg.base_height_mm = 1.0;
        scfg.pillar_base_safety_distance_mm = 1.0;  // support_base_safety_distance 1
        scfg.max_bridges_on_pillar = 3;
        scfg.max_weight_on_model_support = 10.0;
    } else {
        /* „tree" - `make_support_cfg` saka Branching (SLAPrint.cpp:86-110),
           parametrai is V profilio `branchingsupport_*`. Jie KITI, ne tie patys
           su kitu vardu: galvute plonesne (0,4), tiltas trumpesnis (5 mm),
           stulpas platejantis (0,5), jungimas dinaminis. */
        scfg.tree_type = sla::SupportTreeType::Branching;
        scfg.head_front_radius_mm = 0.5 * 0.4;      // branchingsupport_head_front_diameter
        const double pillar_r = 0.5 * 1.0;          // branchingsupport_pillar_diameter
        scfg.head_back_radius_mm = pillar_r;
        scfg.head_fallback_radius_mm = 0.01 * 50.0 * pillar_r;  // small_pillar 50 %
        scfg.head_penetration_mm = 0.2;
        scfg.head_width_mm = 1.0;
        scfg.object_elevation_mm = 0.0;             // zero elevation
        scfg.bridge_slope = 45.0 * M_PI / 180.0;
        scfg.max_bridge_length_mm = 5.0;
        scfg.max_pillar_link_distance_mm = 10.0;
        scfg.pillar_connection_mode = sla::PillarConnectionMode::dynamic;
        scfg.ground_facing_only = false;
        scfg.pillar_widening_factor = 0.5;
        scfg.base_radius_mm = 0.5 * 4.0;            // branchingsupport_base_diameter 4
        scfg.base_height_mm = 1.0;
        scfg.pillar_base_safety_distance_mm = 1.0;
        scfg.max_bridges_on_pillar = 2;
        scfg.max_weight_on_model_support = 10.0;
    }

    sla::PadConfig pcfg;
    pcfg.wall_thickness_mm = 0.15;
    pcfg.wall_slope = 90.0 * M_PI / 180.0;
    pcfg.max_merge_dist_mm = 50.0;
    pcfg.wall_height_mm = 0.0;
    pcfg.brim_size_mm = 1.6;
    pcfg.embed_object.enabled = true;               // pad_around_object 1
    pcfg.embed_object.everywhere = false;
    pcfg.embed_object.object_gap_mm = 1.0;
    pcfg.embed_object.stick_width_mm = 0.5;
    pcfg.embed_object.stick_stride_mm = 10.0;
    pcfg.embed_object.stick_penetration_mm = 0.3;

    sla::SupportableMesh sm{its, pts, scfg};
    sm.pad_cfg = pcfg;

    t0 = Clock::now();
    sla::JobController ctl;
    auto tree = sla::create_support_tree(sm, ctl);
    long t_tree = ms_since(t0);
    std::printf("atramu trikampiu %zu\n", tree.first.indices.size());

    /* ⚠️ NEISTIRTA (2026-08-19): su „tree" padas iseina TUSCIAS (0 mm3), o
       PrusaSlicer tam paciam puodeliui pirmame sluoksnyje turi ~247 mm2
       pedsaka. Bendra derva sutampa per 1,6 %, tad efektas mazas, bet
       priezastis nezinoma - tikrinti pries siulant „tree" naudotojui. */
    t0 = Clock::now();
    indexed_triangle_set pad = sla::create_pad(sm, tree.first, ctl);
    long t_pad = ms_since(t0);
    std::printf("pado trikampiu   %zu\n", pad.indices.size());


    /* Turiai mm3 - kad butu ka lyginti su PrusaSlicer `usedMaterial` (ml). */
    const float v_model = its_volume(its);
    const float v_sup   = its_volume(tree.first);
    const float v_pad   = its_volume(pad);
    std::printf("turis_mm3        modelis %.1f  atramos %.1f  padas %.1f  viso %.4f ml\n",
                v_model, v_sup, v_pad, (v_model + v_sup + v_pad) / 1000.0);
    std::printf("laikas_ms        {\"slice\":%ld,\"prepare\":%ld,\"points\":%ld,\"tree\":%ld,\"pad\":%ld,\"viso\":%ld}\n",
                t_slice, t_prep, t_pts, t_tree, t_pad,
                t_slice + t_prep + t_pts + t_tree + t_pad);
    return 0;
}
