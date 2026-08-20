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
#include <algorithm>
#include <cmath>

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>
#include <libslic3r/AABBMesh.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>
#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/SLA/JobController.hpp>
#include <libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp>
#include <libslic3r/SLA/RasterBase.hpp>
#include <libslic3r/Zipper.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/ElephantFootCompensation.hpp>
#include <libslic3r/SLA/Rotfinder.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/QuadricEdgeCollapse.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

using namespace Slic3r;
using Clock = std::chrono::steady_clock;

/*
 * FDM miniatiuru pakaitalas.
 *
 * `PrintConfig.cpp` savo tikrinimuose kviecia `GCodeThumbnails` funkcijas, o
 * ju tikrasis failas (`GCode/Thumbnails.cpp`) tempia libjpeg. Mums to nereikia:
 * miniatiuros yra FDM G-code dalykas, SLA grandine ju neliecia. Iki
 * `sla_rotfind` tai nekliuvo, nes visa `print_config_def` dalis buvo negyva -
 * Rotfinder yra pirmas, kuriam prireike tikro `SLAPrintObjectConfig`.
 *
 * Kaip ir kiti pakaitalai (LibBGCode, boost::log) - aplinkos, ne algoritmo
 * keitimas: sios dvi funkcijos musu kelyje niekada nekvieciamos.
 */
#include <libslic3r/GCode/Thumbnails.hpp>
namespace Slic3r { namespace GCodeThumbnails {
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors>
make_and_check_thumbnail_list(const std::string &, const std::string_view)
{
    return {};
}
std::string get_error_string(const ThumbnailErrors &) { return {}; }
}} // namespace Slic3r::GCodeThumbnails


/*
 * Eigos pranesimas i JS puse. Butinas todel, kad pjaustymas yra blokuojantis:
 * biustui tai 20 s, per kurias naudotojas turi matyti, kas vyksta.
 *
 * Kvieciama globali `window.slaProgress(etapas, proc)` - jei jos nera (pvz.
 * komandineje eiluteje), nieko nedaro.
 */
static void praneskEiga(const char *etapas, int proc)
{
#ifdef __EMSCRIPTEN__
    MAIN_THREAD_EM_ASM({
        var f = (typeof self !== "undefined" && self.slaProgress) || null;
        if (f) f(UTF8ToString($0), $1);
    }, etapas, proc);
#else
    (void) etapas; (void) proc;
#endif
}

/* Plokste is V profilio (`bed_shape = 0x0,40.8x0,40.8x30.6,0x30.6`). */
static constexpr double PLOKSTE_X_MM = 40.8;
static constexpr double PLOKSTE_Y_MM = 30.6;
/* `initial_layer_height` is V profilio - pirmas sluoksnis storesnis. */
static constexpr double PIRMO_SLUOKSNIO_MM = 0.3;

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

/*
 * Rastro perkelimas: modelio nulis yra plokstes CENTRAS, o rastro (0,0) - jos
 * kampas, tad piesiant reikia prideti puse plokstes. Be sito modelis atsiduria
 * kampe ir nukerpamas - printerio sesija tai pagavo ismatavusi PNG (objekto
 * centras 273,194 vietoj 160,120).
 */
static sla::RasterBase::Trafo rastro_trafo()
{
    sla::RasterBase::Trafo tr{sla::RasterBase::roLandscape, sla::RasterBase::MirrorX};
    tr.center_x = scaled<coord_t>(PLOKSTE_X_MM / 2);
    tr.center_y = scaled<coord_t>(PLOKSTE_Y_MM / 2);
    return tr;
}

/*
 * Sluoksniu tinklelis PRINTERIUI - toks pat, kaip `SLAPrintSteps.cpp:558-564`:
 * pirmas sluoksnis storesnis (`initial_layer_height`, V profilyje 0,3 mm) ir
 * pjaunamas per savo VIDURI, o toliau eina iprasti 0,05 mm.
 *
 * Del to musu failas anksciau turejo 300 sluoksniu, kai PrusaSlicer - 295
 * (0,3/0,05 - 1 = 5), o raftas issitesdavo per tris sluoksnius vietoj vieno.
 */
static std::vector<float> sluoksniu_tinklelis(double zmin, double zmax,
                                              double lh, double ilh)
{
    /* ⚠️ Skaiciuojam SVEIKAISIAIS (scaled), kaip originalas - ne `float`'ais.
       Su slankiuoju kableliu paskutinis sluoksnis kartais prasprudzia pro
       `h <= zmax` (15,149999 vs 15,15), ir failas iseina vienu sluoksniu
       trumpesnis: puodelis turejo 294, o PrusaSlicer - 295. */
    std::vector<float> z;
    const coord_t minZs = scaled<coord_t>(zmin), maxZs = scaled<coord_t>(zmax);
    const coord_t lhs = scaled<coord_t>(lh), ilhs = scaled<coord_t>(ilh);
    if (maxZs <= minZs) return z;

    z.push_back(float(unscaled<double>(minZs) + ilh / 2.0));
    for (coord_t h = minZs + ilhs + lhs; h <= maxZs; h += lhs)
        z.push_back(float(unscaled<double>(h) - lh / 2.0));
    return z;
}

/* Rezultatas laikomas cia, kad JS pusei uztektu grazinti rodykle. */
static std::string g_json;

/*
 * Paskutinio pjaustymo rezultatas. Laikomas todel, kad `.sl1` gamyba yra
 * ATSKIRAS zingsnis: naudotojas pirma pamato, kiek dervos ir kur atramos, ir tik
 * tada spaudzia „siusti i printeri".
 */
static struct {
    indexed_triangle_set model, supports, pad;
    double layer_h = 0.05;
    bool ready = false;
} g_last;

/**
 * Visa grandine. `path` - failas Emscripten failu sistemoje (arba tikras
 * failas, kai kompiliuojama ne i WASM).
 */
static const char *run_chain(TriangleMesh &mesh, double layer_h, bool branching,
                             bool centruoti, bool verbose)
{
    char buf[2048];
    /* Modelis pastatomas ant plokstes taip pat, kaip tai daro PrusaSlicer,
       ikeldamas STL: XY - i plokstes vidurį, Z - ant nulio. Rasterizatorius
       pats nieko necentruoja (SL1.cpp:497-528 palieka `Trafo.center` nuliuose),
       tad be sito modelis atsiduria rastro kampe ir dalis jo nukerpama. */
    /*
     * KOORDINACIU SUSITARIMAS (2026-08-19, po printerio sesijos radinio):
     * variklio nulis yra PLOKSTES CENTRAS, o ne kampas - lygiai taip, kaip
     * duoda pulto `place()` (slicer.js:132-136: XY centras i nuli, apacia ant
     * plokstes). Rastre puse plokstes prideda pats rasterizatorius per
     * `Trafo.center` (zr. `rastro_trafo`).
     *
     * Kai modelis ateina is failo, ji cia ir pastatom taip pat.
     */
    if (centruoti) {
        const auto bb0 = mesh.bounding_box();
        const Vec3d c = bb0.center();
        mesh.translate(float(-c.x()), float(-c.y()), float(-bb0.min.z()));
    }

    const indexed_triangle_set &its = mesh.its;
    if (verbose) {
        std::printf("trikampiu        %zu\n", its.indices.size());
        std::printf("atramu tipas     %s\n", branching ? "tree (branching)" : "regular (default)");
    }

    praneskEiga("pjaustomas modelis", 5);
    const auto bb = mesh.bounding_box();
    std::vector<float> grid;
    for (float z = float(bb.min.z()) + float(layer_h) / 2.f; z < float(bb.max.z()); z += float(layer_h))
        grid.push_back(z);
    if (verbose) std::printf("sluoksniu        %zu\n", grid.size());

    auto t0 = Clock::now();
    std::vector<ExPolygons> slices = slice_mesh_ex(its, grid, 0.005f);   // slice_closing_radius
    const long t_slice = ms_since(t0);

    praneskEiga("ieskoma, kur reikia atramu", 25);
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
    praneskEiga("sejami atramu taskai", 55);
    sla::LayerSupportPoints layer_pts = sla::generate_support_points(data, gen_cfg);
    const long t_pts = ms_since(t0);
    if (verbose) std::printf("tasku            %zu\n", layer_pts.size());

    AABBMesh emesh{its};
    sla::SupportPoints pts = sla::move_on_mesh_surface(layer_pts, emesh, 1.0);

    sla::SupportableMesh sm{its, pts, make_tree_cfg(branching)};
    sm.pad_cfg = make_pad_config();

    praneskEiga("statomos atramos", 65);
    t0 = Clock::now();
    sla::JobController ctl;
    auto tree = sla::create_support_tree(sm, ctl);
    const long t_tree = ms_since(t0);
    if (verbose) std::printf("atramu trikampiu %zu\n", tree.first.indices.size());

    /*
     * ISTIRTA 2026-08-19 (buvo itariama kaip beda - NEPASITVIRTINO).
     *
     * Su „tree" padas kartais iseina TUSCIAS, ir taip ELGIASI PATS ORIGINALAS.
     * Priezastis: `_AroundPadSkeleton::remove_redundant_parts` (Pad.cpp:307-316)
     * palieka tik tas pado dalis, po kuriomis yra ATRAMA. Medzio atramos daznai
     * remiasi i pati modeli ir plokstes nesiekia, tad ju po padu ir nera.
     *
     * Ismatuota:
     *   puodelis  regular padas 54,3 mm3 · tree 0     (atramos prasideda 1,44 mm)
     *   biustas   regular padas 61,4 mm3 · tree 36,1  (kojos siekia plokste)
     * PrusaSlicer tam paciam puodeliui su tree: pirmi sluoksniai 15212 -> 15400
     * -> 15544 px, t. y. tik modelio konturas, jokio pado apvado (su regular jo
     * pirmas sluoksnis 37428 px). Elgesys sutampa.
     */
    praneskEiga("dedamas raftas", 92);
    t0 = Clock::now();
    indexed_triangle_set pad = sla::create_pad(sm, tree.first, ctl);
    const long t_pad = ms_since(t0);
    if (verbose) std::printf("pado trikampiu   %zu\n", pad.indices.size());

    /*
     * Diagnostika: kuris atramu elementas nusileidzia zemiausiai. Reikia todel,
     * kad viena atrama Terry_The_Dragon modelyje nusileido i -0,658 mm, t. y.
     * PO plokste (printerio sesija pagavo 3D vaizde), o PrusaSlicer to nedaro.
     */
    if (verbose) {
        const auto &t = tree.second;
        auto zmin_v = [](double a, double b2) { return a < b2 ? a : b2; };
        double zp = 1e30, zh = 1e30, zj = 1e30, zb = 1e30, zcb = 1e30, zdb = 1e30, zped = 1e30, za = 1e30;
        for (const auto &x : t.pillars)      zp   = zmin_v(zp,  x.endpt.z());
        for (const auto &x : t.heads)        zh   = zmin_v(zh,  x.junction_point().z() - x.width_mm - x.r_back_mm);
        for (const auto &x : t.junctions)    zj   = zmin_v(zj,  x.pos.z() - x.r);
        for (const auto &x : t.bridges)      zb   = zmin_v(zb,  zmin_v(x.startp.z(), x.endp.z()) - x.r);
        for (const auto &x : t.crossbridges) zcb  = zmin_v(zcb, zmin_v(x.startp.z(), x.endp.z()) - x.r);
        for (const auto &x : t.diffbridges)  zdb  = zmin_v(zdb, zmin_v(x.startp.z(), x.endp.z()) - x.r);
        for (const auto &x : t.pedestals)    zped = zmin_v(zped, x.pos.z());
        for (const auto &x : t.anchors)      za   = zmin_v(za,  x.junction_point().z() - x.width_mm - x.r_back_mm);
        std::printf("zemiausi         stulpai %.3f · galvutes %.3f · jungtys %.3f · tiltai %.3f\n"
                    "                 kryzminiai %.3f · ant modelio %.3f · pedos %.3f · inkarai %.3f\n",
                    zp, zh, zj, zb, zcb, zdb, zped, za);
        std::printf("kiekiai          stulpu %zu · galvuciu %zu · jungciu %zu · tiltu %zu · pedu %zu · inkaru %zu\n",
                    t.pillars.size(), t.heads.size(), t.junctions.size(),
                    t.bridges.size(), t.pedestals.size(), t.anchors.size());
    }

    /* Kiekvienos dalies Z ribos - is ju is karto matyti, jei kas nors nusileido
       PO plokste (printerio sesija tokia atrama pagavo Terry_The_Dragon). */
    auto z_ribos = [](const indexed_triangle_set &m, float &lo, float &hi) {
        lo = 1e30f; hi = -1e30f;
        for (const auto &v : m.vertices) { if (v.z() < lo) lo = v.z(); if (v.z() > hi) hi = v.z(); }
        if (m.vertices.empty()) { lo = hi = 0.f; }
    };
    float mz0, mz1, sz0, sz1, pz0, pz1;
    z_ribos(its, mz0, mz1);
    z_ribos(tree.first, sz0, sz1);
    z_ribos(pad, pz0, pz1);

    const float v_model = its_volume(its);
    const float v_sup   = its_volume(tree.first);
    const float v_pad   = its_volume(pad);
    if (verbose)
        std::printf("z_ribos          modelis %.3f..%.3f  atramos %.3f..%.3f  padas %.3f..%.3f\n",
                    mz0, mz1, sz0, sz1, pz0, pz1);
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
    g_last.model = its;
    g_last.supports = tree.first;
    g_last.pad = pad;
    g_last.layer_h = layer_h;
    g_last.ready = true;

    /*
     * Geometrija VAIZDUI. Apkerpama ties plokste, nes 3D vaizdas turi rodyti
     * ta pati, ka printeris darys: kas zemiau nulio, i sluoksnius nepatenka
     * (zr. `sluoksniu_tinklelis` ir SLAPrintSteps.cpp:693), tad ir ekrane to
     * buti neturi. Kitaip zmogus mato guzą po raftu, kurio spaudinyje nebus.
     *
     * Vidine geometrija (`g_last`) lieka NEPALIESTA - diagnostika toliau rodo
     * tikras Z ribas, tad klausimas „kodel ta atrama ten leidziasi" nera
     * paslepiamas.
     */
    auto apkirpk = [](const indexed_triangle_set &m, float z0) {
        /* Virsunes, nusileidusios zemiau plokstes, PRIREMIAMOS prie jos. Taip
           vaizde nelieka nieko po plokste, o forma virsuje nepasikeicia - t. y.
           matosi lygiai tai, ka printeris atspausdins (jo sluoksniai irgi
           prasideda nuo plokstes). Trikampiu neismetam: kitaip liktu skyle. */
        indexed_triangle_set o = m;
        for (auto &v : o.vertices)
            if (v.z() < z0) v.z() = z0;
        return o;
    };
    const float PLOKSTE_Z = 0.f;

    its_write_stl_binary("/out_model.stl", "modelis", its);
    its_write_stl_binary("/out_supports.stl", "atramos", apkirpk(tree.first, PLOKSTE_Z));
    its_write_stl_binary("/out_pad.stl", "padas", pad);

    std::snprintf(buf, sizeof(buf),
        "{\"tipas\":\"%s\",\"trikampiu\":%zu,\"sluoksniu\":%zu,\"tasku\":%zu,"
        "\"z\":{\"modelis\":[%.3f,%.3f],\"atramos\":[%.3f,%.3f],\"padas\":[%.3f,%.3f]},"
        "\"atramu_trikampiu\":%zu,\"pado_trikampiu\":%zu,"
        "\"turis\":{\"modelis\":%.1f,\"atramos\":%.1f,\"padas\":%.1f,\"viso_ml\":%.4f},"
        "\"laikas_ms\":{\"slice\":%ld,\"prepare\":%ld,\"points\":%ld,\"tree\":%ld,"
        "\"pad\":%ld,\"viso\":%ld}}",
        branching ? "tree" : "regular",
        its.indices.size(), grid.size(), layer_pts.size(),
        mz0, mz1, sz0, sz1, pz0, pz1,
        tree.first.indices.size(), pad.indices.size(),
        v_model, v_sup, v_pad, (v_model + v_sup + v_pad) / 1000.0,
        t_slice, t_prep, t_pts, t_tree, t_pad,
        t_slice + t_prep + t_pts + t_tree + t_pad);
    praneskEiga("baigta", 100);
    g_json = buf;
    return g_json.c_str();
}


extern "C" {

/**
 * Iejimas is JS su FAILU: STL nuskaitomas is modulio failu sistemos ir
 * pastatomas i plokstes vidurį (kaip PrusaSlicer, ikeldamas STL).
 */
EMSCRIPTEN_KEEPALIVE
const char *sla_slice(const char *path, double layer_h, int branching)
{
    TriangleMesh mesh;
    if (!mesh.ReadSTLFile(path)) {
        g_json = "{\"klaida\":\"STL neperskaitytas\"}";
        return g_json.c_str();
    }
    return run_chain(mesh, layer_h, branching != 0, true, false);
}

/**
 * Iejimas is pulto: trikampiai paduodami TIESIAI is atminties (9 float vienam),
 * jau pasukti ir pastatyti - pultas tai daro pats, tad cia NECENTRUOJAM.
 */
EMSCRIPTEN_KEEPALIVE
const char *sla_slice_mesh(const float *pos, int ntri, double layer_h, int branching)
{
    if (!pos || ntri <= 0) {
        g_json = "{\"klaida\":\"tuscias tinklas\"}";
        return g_json.c_str();
    }
    indexed_triangle_set its;
    its.vertices.reserve(size_t(ntri) * 3);
    its.indices.reserve(size_t(ntri));
    for (int t = 0; t < ntri; ++t) {
        const int o = t * 9;
        its.vertices.emplace_back(pos[o + 0], pos[o + 1], pos[o + 2]);
        its.vertices.emplace_back(pos[o + 3], pos[o + 4], pos[o + 5]);
        its.vertices.emplace_back(pos[o + 6], pos[o + 7], pos[o + 8]);
        its.indices.emplace_back(t * 3, t * 3 + 1, t * 3 + 2);
    }
    /* Sulipdom sutampancias virsunes - kitaip `its_face_neighbors` neranda
       kaimynystes, o nuo jos priklauso normales ir nuokabu paieska. */
    its_merge_vertices(its, true);

    /* ⚠️ Apverstos normales. Jei tinklo turis neigiamas, trikampiai sukti i
       VIDU - tada ne tik derva iseina su minusu (printerio sesija pagavo
       `rawMl: -1`), bet ir nuokabu paieska ziuri i ne ta puse. Apsukam.
       PrusaSlicer tai daro `repair()` metu; mums uztenka sio patikrinimo. */
    if (its_volume(its) < 0.f)
        for (auto &t : its.indices) std::swap(t[1], t[2]);

    TriangleMesh mesh{std::move(its)};
    return run_chain(mesh, layer_h, branching != 0, false, false);
}

/**
 * Prusos automatinis pastatymas (Rotfinder.cpp) - TAS PATS kodas, kuri
 * PrusaSlicer GUI kviecia is „Optimize orientation". Nieko cia neinterpretuojam:
 * pastatom `ModelObject` ir kvieciam tris jo tikslus.
 *
 * `kuris` - bitu kauke: 1 = maziausiai atramu, 2 = zemiausias spaudinys,
 * 4 = pavirsiaus lygiavimas (pastarasis brangus - 961 ivertinimas per VISUS
 * trikampius, tad paduodamas atskirai).
 *
 * Grazina kampus RADIANAIS apie X ir Y. Taikymo tvarka - kaip
 * `to_transform3f` (Rotfinder.cpp): pirma X, paskui Y.
 */
EMSCRIPTEN_KEEPALIVE
const char *sla_rotfind(const float *pos, int ntri, int kuris,
                        double tikslumas, int max_trikampiu)
{
    if (!pos || ntri <= 0) {
        g_json = "{\"klaida\":\"tuscias tinklas\"}";
        return g_json.c_str();
    }
    indexed_triangle_set its;
    its.vertices.reserve(size_t(ntri) * 3);
    its.indices.reserve(size_t(ntri));
    for (int t = 0; t < ntri; ++t) {
        const int o = t * 9;
        its.vertices.emplace_back(pos[o + 0], pos[o + 1], pos[o + 2]);
        its.vertices.emplace_back(pos[o + 3], pos[o + 4], pos[o + 5]);
        its.vertices.emplace_back(pos[o + 6], pos[o + 7], pos[o + 8]);
        its.indices.emplace_back(t * 3, t * 3 + 1, t * 3 + 2);
    }
    its_merge_vertices(its, true);
    if (its_volume(its) < 0.f)
        for (auto &t : its.indices) std::swap(t[1], t[2]);

    /*
     * Supaprastinimas PRIES paieska (2026-08-20, printerio sesijos prasymas #2).
     *
     * Kaina yra tiesiogine: kiekvienas isgaubtinio apvalkalo kandidatas
     * ivertinamas per VISUS trikampius, tad drakonui (1,19 mln.) tai 13 s.
     * Pastatymas yra bendros formos klausimas, ne detaliu, todel ivertinimui
     * uztenka retesnio tinklo - o SUSLICINAMAS lieka pilnas modelis.
     *
     * ⚠️ Tai musu sprendimas, ne originalo elgesys (PrusaSlicer vertina pilna
     * tinkla, bet daro tai 8 gijomis - ju TBB pas mus pakeistas nuosekliu
     * ciklu). Todel jis ijungiamas TIK is JS ir tik pasakius, kiek trikampiu
     * palikti; be to reikia patikrinti, ar atsakymas nepasikeicia (zr.
     * `slicer-lab/rot.mjs --retinti`).
     */
    size_t po_retinimo = its.indices.size();
    float turis_po = 0.f;
    if (max_trikampiu > 0 && its.indices.size() > size_t(max_trikampiu)) {
        try {
            its_quadric_edge_collapse(its, uint32_t(max_trikampiu));
            /* Po suretinimo tinklas gali buti sulipdytas kitaip - virsuniu
               tvarka, o su ja ir normaliu kryptis. Tikrinam ta pati, ka
               tikrinam ikeldami: neigiamas turis = trikampiai sukti i vidu. */
            its_merge_vertices(its, true);
            if (its_volume(its) < 0.f)
                for (auto &t : its.indices) std::swap(t[1], t[2]);
        } catch (...) {
            /* Nepavyko suretinti - dirbam su pilnu tinklu. Letai, bet teisingai. */
        }
        po_retinimo = its.indices.size();
        turis_po = its_volume(its);
    }

    Model model;
    ModelObject *mo = model.add_object();
    mo->add_volume(TriangleMesh{its});
    mo->add_instance();

    /* Tas pats profilis, kaip pjaustant: padas aplink modeli ir nulinis
       pakelimas. Tai svarbu - nuo to priklauso, KURIUO keliu Rotfinder eina
       (Rotfinder.cpp `is_on_floor`): gulinciam ant plokstes jis tikrina tik
       isgaubtinio apvalkalo sienas, pakeltam - tinkleli per visus kampus. */
    DynamicPrintConfig cfg;
    cfg.set_key_value("pad_around_object", new ConfigOptionBool(true));
    cfg.set_key_value("support_object_elevation", new ConfigOptionFloat(0.));

    sla::RotOptimizeParams p;
    /* `accuracy` valdo `max_tries`. ⚠️ TIK tam keliui, kuris eina per
       optimizatoriaus tinkleli (pakeltas modelis). Gulinciam ant plokstes -
       o pas mus butent taip - `get_chull_rotations` ji gauna kaip `max_count`,
       bet paskutinis jos ciklas (Rotfinder.cpp:260-261) sudeda VISUS kandidatus:
       `max_count` panaudojamas tik `reserve_vector` dydziui. Isvada: musu kelyje
       tai NIEKO nepagreitina. Patikrinta ir matavimu, ne tik akimis. */
    p.accuracy(tikslumas > 0 ? float(tikslumas) : 1.f).print_config(&cfg);

    /*
     * Eiga (printerio sesijos prasymas #1). `Rotfinder.cpp` kviecia si atgalini
     * rysi kas iteracija, tad juosta gali judeti tikrai, o ne kaboti ties 5 %.
     *
     * ⚠️ NUTRAUKTI (Stop) siuo keliu NEIMANOMA: grazintas `false` paieska
     * sustabdytu, bet mes sedim WORKER'io gijoje, kuri viso skaiciavimo metu
     * blokuota - `postMessage` is pulto tiesiog laukia eileje ir jokia velevele
     * cia nepasiekia. Reiketu SharedArrayBuffer, o jam - COOP/COEP antrasciu,
     * kuriu gh-pages nustatyti negalima.
     */
    /* Kandidatu skaitiklis. `find_min_score` kiekvienam kandidatui pirma
       klausia `stopcond()`, o ta klausia musu su -1 - tad neigiamu kvietimu
       kiekis lygus kandidatu skaiciui. Reikia todel, kad butu galima pasakyti,
       is ko susideda laikas: kandidatai x trikampiai (klausimas is #108). */
    static int kandidatu = 0;
    kandidatu = 0;
    p.statucb([](int proc) {
        if (proc < 0) { ++kandidatu; return true; }
        praneskEiga("ieskoma geriausios padeties", proc);
        return true;
    });

    char diag[128];
    std::snprintf(diag, sizeof(diag), "\"retinta\":{\"trikampiu\":%zu,\"turis\":%.1f}",
                  po_retinimo, turis_po);
    /* `kandidatu` uzpildomas jau kviciant, tad i JSON deda `irasyk` pabaigoje. */
    std::string js = "{";
    js += diag;
    auto irasyk = [&js](const char *vardas, const Vec2d &r, long ms) {
        char b[192];
        std::snprintf(b, sizeof(b),
            "%s\"%s\":{\"rx\":%.6f,\"ry\":%.6f,\"ms\":%ld}",
            js.size() > 1 ? "," : "", vardas, r.x(), r.y(), ms);
        js += b;
    };

    if (kuris & 1) {
        auto t0 = Clock::now();
        Vec2d r = sla::find_least_supports_rotation(*mo, p);
        irasyk("maziausiai_atramu", r, ms_since(t0));
    }
    if (kuris & 2) {
        auto t0 = Clock::now();
        Vec2d r = sla::find_min_z_height_rotation(*mo, p);
        irasyk("zemiausias", r, ms_since(t0));
    }
    if (kuris & 4) {
        auto t0 = Clock::now();
        Vec2d r = sla::find_best_misalignment_rotation(*mo, p);
        irasyk("lygiavimas", r, ms_since(t0));
    }
    char kd[64];
    std::snprintf(kd, sizeof(kd), ",\"kandidatu\":%d", kandidatu);
    js += kd;
    js += "}";
    g_json = js;
    return g_json.c_str();
}

} // extern "C"



/*
 * Sluoksniai ir `.sl1` archyvas.
 *
 * Formatas tas pats, kuri siuncia PrusaSlicer, ir kuri musu firmware jau moka
 * ispakuoti: ZIP su `config.ini` ir PNG sluoksniais.
 *
 * Piesiama SL1 rasterizatoriumi (`create_raster_grayscale_aa`) su tais paciais
 * ekrano parametrais, kaip V profilyje: 320x240 px ant 40,8x30,6 mm, veidrodis
 * per X (`display_mirror_x = 1`).
 */
namespace {

struct EkranoCfg {
    size_t px_x = 320, px_y = 240;
    double plotis_mm = PLOKSTE_X_MM, aukstis_mm = PLOKSTE_Y_MM;
    bool veidrodis_x = true;
    double gama = 1.0;
    double ekspozicija = 10.0, ekspozicija_pirmo = 15.0;
    size_t pirmu_sluoksniu = 8, fade = 5;
};

std::string ini_eilute(const EkranoCfg &c, double layer_h, size_t sluoksniu,
                       const std::string &vardas, double ml)
{
    char b[1400];
    std::snprintf(b, sizeof(b),
        "action = print\n"
        "expTime = %.6g\n"
        "expTimeFirst = %.6g\n"
        "fileCreationTimestamp = -\n"
        "hollow = 0\n"
        "jobDir = %s\n"
        "layerHeight = %.6g\n"
        "materialName = - default -\n"
        "numFade = %zu\n"
        "numFast = %zu\n"
        "numSlow = %zu\n"
        "printProfile = TinyMaker WASM\n"
        "printTime = 0\n"
        "printerModel = SL1\n"
        "printerProfile = TinyMaker\n"
        "printerVariant = default\n"
        "prusaSlicerVersion = libslic3r-WASM\n"
        "usedMaterial = %.6f\n",
        c.ekspozicija, c.ekspozicija_pirmo, vardas.c_str(), layer_h,
        c.fade, sluoksniu > c.pirmu_sluoksniu ? sluoksniu - c.pirmu_sluoksniu : sluoksniu,
        c.pirmu_sluoksniu, ml);
    return b;
}

} // namespace

extern "C" {

/**
 * Pagamina `.sl1` is paskutinio pjaustymo rezultato.
 * Grazina JSON su sluoksniu skaiciumi ir failo dydziu.
 */
EMSCRIPTEN_KEEPALIVE
const char *sla_export_sl1(const char *out_path, const char *job_name)
{
    if (!g_last.ready) {
        g_json = "{\"klaida\":\"pirma reikia suslicinti\"}";
        return g_json.c_str();
    }
    const EkranoCfg cfg;
    const double layer_h = g_last.layer_h;
    auto t0 = Clock::now();

    /* Bendras Z tinklelis: nuo zemiausio tasko (padas yra po modeliu) iki
       auksciausio. Sluoksnio VIDURYS, kaip ir pjaustant modeli. */
    /*
     * ⚠️ Apacia imama is MODELIO ir PADO, o NE is atramu.
     *
     * PrusaSlicer sluoksniuoja nuo `zoffset = mesh.min.z()` (SLAPrintSteps.cpp:693),
     * t. y. nuo modelio apacios - viskas, kas nusileidzia zemiau, i faila
     * nepatenka, nes printeris zemiau plokstes nespausdina.
     *
     * Musu tinklelis anksciau prasidedavo nuo ZEMIAUSIO tasko is visu daliu, ir
     * viena atrama, nusileidusi i -0,658 mm (Terry_The_Dragon), tapdavo pirmais
     * astuoniais sluoksniais: printeris pradedavo spausdinti pavieni rutuliuka
     * ant plokstes vietoj rafto. Pagavo printerio sesija 3D vaizde.
     */
    float zmin = 1e30f, zmax = -1e30f;
    for (const indexed_triangle_set *m : {&g_last.model, &g_last.pad})
        for (const auto &v : m->vertices)
            if (v.z() < zmin) zmin = v.z();
    for (const indexed_triangle_set *m : {&g_last.model, &g_last.supports, &g_last.pad})
        for (const auto &v : m->vertices)
            if (v.z() > zmax) zmax = v.z();
    std::vector<float> grid = sluoksniu_tinklelis(zmin, zmax, layer_h, PIRMO_SLUOKSNIO_MM);

    /* Modelis ir atramos pjaustomi atskirai, tada sujungiami - kaip SLAPrint. */
    std::vector<ExPolygons> mo = slice_mesh_ex(g_last.model, grid, 0.005f);
    sla::JobController ctl;
    std::vector<ExPolygons> su = sla::slice(g_last.supports, g_last.pad, grid, 0.005f, ctl);

    const sla::Resolution res{cfg.px_x, cfg.px_y};
    const sla::PixelDim   pxd{cfg.plotis_mm / cfg.px_x, cfg.aukstis_mm / cfg.px_y};
    const sla::RasterBase::Trafo tr = rastro_trafo();

    Zipper zip(out_path, Zipper::FAST_COMPRESSION);
    const std::string vardas = job_name && *job_name ? job_name : "spaudinys";
    size_t irasyta = 0, baitu = 0;

    /*
     * „Dramblio peda": pirmi sluoksniai issiplecia, nes derva prie plokstes
     * kietinama ilgiau. PrusaSlicer tai kompensuoja SUTRAUKDAMAS pirmus
     * `faded_layers` sluoksniu, ir kompensacija tiesiskai mazeja iki nulio
     * (`apply_printer_corrections`, PrinterCorrections.cpp:29-42):
     *     efc(i) = (fade-1 - i) * start / (fade-1)
     * V profilyje: elefant_foot_compensation 0,2 · min_width 0,2 · faded 5.
     * Taikoma ir modeliui, ir atramoms atskirai - kaip originale.
     */
    const double EFC = 0.2, EFC_MIN_W = 0.2 / 2.0;
    const size_t FADE = 5, FADE_EFC = FADE > 1 ? FADE - 1 : 1;
    auto efc = [&](size_t i) { return (FADE_EFC - i) * EFC / FADE_EFC; };

    for (size_t i = 0; i < grid.size(); ++i) {
        if ((i & 63) == 0)
            praneskEiga("gaminami sluoksniai", int(100.0 * i / grid.size()));
        if (i < FADE && EFC > 0.) {
            mo[i] = elephant_foot_compensation(mo[i], float(EFC_MIN_W), float(efc(i)));
            if (i < su.size() && !su[i].empty())
                su[i] = elephant_foot_compensation(su[i], float(EFC_MIN_W), float(efc(i)));
        }
        ExPolygons sluoksnis = mo[i];
        if (i < su.size() && !su[i].empty()) {
            ExPolygons visi = sluoksnis;
            visi.insert(visi.end(), su[i].begin(), su[i].end());
            sluoksnis = union_ex(visi);
        }
        auto rst = sla::create_raster_grayscale_aa(res, pxd, cfg.gama, tr);
        for (const ExPolygon &ex : sluoksnis) rst->draw(ex);
        sla::EncodedRaster png = rst->encode(sla::PNGRasterEncoder{});

        char nm[128];
        std::snprintf(nm, sizeof(nm), "%s/%05zu.png", vardas.c_str(), i + 1);
        zip.add_entry(nm, png.data(), png.size());
        baitu += png.size();
        ++irasyta;
    }

    /* Dervos kiekis - is turiu, kaip ir rodome naudotojui. */
    const double ml = (its_volume(g_last.model) + its_volume(g_last.supports)
                       + its_volume(g_last.pad)) / 1000.0;
    const std::string ini = ini_eilute(cfg, layer_h, irasyta, vardas, ml);
    zip.add_entry("config.ini", ini.c_str(), ini.size());
    zip.finalize();

    char b[512];
    std::snprintf(b, sizeof(b),
        "{\"sluoksniu\":%zu,\"png_baitu\":%zu,\"laikas_ms\":%ld,\"failas\":\"%s\"}",
        irasyta, baitu, ms_since(t0), out_path);
    g_json = b;
    return g_json.c_str();
}

} // extern "C"


/*
 * Perziuros kaukes pultui.
 *
 * Pultas piesia sluoksniu perziura is 0/1 kaukiu (senojo modulio formatas:
 * `slices`, `gw`, `gh`, `modelH`). Mes duodam DVI serijas - modelio ir atramu -
 * kad atramas butu galima nudazyti KITA spalva tiksliai, o ne apytiksliais
 * diskais, kaip anksciau.
 *
 * Rezultatas rasomas i `/preview.bin`:
 *   antraste: uint32 kiekis, uint32 w, uint32 h, float modelio_aukstis
 *   toliau:   kiekis x (w*h modelio baitu + w*h atramu baitu), po 0 arba 1
 */
extern "C" {

EMSCRIPTEN_KEEPALIVE
const char *sla_preview(const char *out_path, int max_sluoksniu)
{
    if (!g_last.ready) {
        g_json = "{\"klaida\":\"pirma reikia suslicinti\"}";
        return g_json.c_str();
    }
    const int W = 320, H = 240;            // toks pat tinklelis, kaip pulto RES
    const double layer_h = g_last.layer_h;

    /*
     * ⚠️ Apacia imama is MODELIO ir PADO, o NE is atramu.
     *
     * PrusaSlicer sluoksniuoja nuo `zoffset = mesh.min.z()` (SLAPrintSteps.cpp:693),
     * t. y. nuo modelio apacios - viskas, kas nusileidzia zemiau, i faila
     * nepatenka, nes printeris zemiau plokstes nespausdina.
     *
     * Musu tinklelis anksciau prasidedavo nuo ZEMIAUSIO tasko is visu daliu, ir
     * viena atrama, nusileidusi i -0,658 mm (Terry_The_Dragon), tapdavo pirmais
     * astuoniais sluoksniais: printeris pradedavo spausdinti pavieni rutuliuka
     * ant plokstes vietoj rafto. Pagavo printerio sesija 3D vaizde.
     */
    float zmin = 1e30f, zmax = -1e30f;
    for (const indexed_triangle_set *m : {&g_last.model, &g_last.pad})
        for (const auto &v : m->vertices)
            if (v.z() < zmin) zmin = v.z();
    for (const indexed_triangle_set *m : {&g_last.model, &g_last.supports, &g_last.pad})
        for (const auto &v : m->vertices)
            if (v.z() > zmax) zmax = v.z();
    std::vector<float> visi = sluoksniu_tinklelis(zmin, zmax, layer_h, PIRMO_SLUOKSNIO_MM);
    if (visi.empty()) { g_json = "{\"klaida\":\"nera sluoksniu\"}"; return g_json.c_str(); }

    /* Imam tik dali sluoksniu - perziurai uztenka, o kiekvienas kainuoja. */
    const int N = std::min<int>(max_sluoksniu > 0 ? max_sluoksniu : 160, int(visi.size()));
    std::vector<float> imami(N);
    for (int k = 0; k < N; ++k)
        imami[k] = visi[N > 1 ? size_t(std::llround(double(k) * (visi.size() - 1) / (N - 1))) : 0];

    sla::JobController ctl;
    std::vector<ExPolygons> mo = slice_mesh_ex(g_last.model, imami, 0.005f);
    std::vector<ExPolygons> su = sla::slice(g_last.supports, g_last.pad, imami, 0.005f, ctl);

    const sla::Resolution res{size_t(W), size_t(H)};
    const sla::PixelDim   pxd{PLOKSTE_X_MM / W, PLOKSTE_Y_MM / H};
    const sla::RasterBase::Trafo tr = rastro_trafo();

    /* Kaukes gaminam per ta pati rasterizatoriu: `gamma = 0` isjungia
       minkstinima, tad iskart gaunam 0/1, be jokiu tarpiniu atspalviu. */
    auto kauke = [&](const ExPolygons &ex, std::vector<uint8_t> &out) {
        auto rst = sla::create_raster_grayscale_aa(res, pxd, 0.0, tr);
        for (const ExPolygon &e : ex) rst->draw(e);
        sla::EncodedRaster ppm = rst->encode(sla::PPMRasterEncoder{});
        const uint8_t *d = static_cast<const uint8_t *>(ppm.data());
        /* PPM antraste cia atskirta TARPAIS, ne naujomis eilutemis:
           "P5 320 240 255 " (RasterBase.cpp:57-59). Ieskant naujos eilutes
           praeini pro visus duomenis ir gauni tuscia kauke - taip ir nutiko
           pirmame bandyme. Praleidziam keturis tarpus. */
        size_t p = 0, tarpu = 0;
        while (p < ppm.size() && tarpu < 4) { if (d[p] == ' ') tarpu++; p++; }
        out.assign(size_t(W) * H, 0);
        for (size_t i = 0; i < out.size() && p + i < ppm.size(); ++i)
            out[i] = d[p + i] > 127 ? 1 : 0;
    };

    std::vector<uint8_t> a, s;
    FILE *f = std::fopen(out_path, "wb");
    if (!f) { g_json = "{\"klaida\":\"nepavyko sukurti failo\"}"; return g_json.c_str(); }
    const uint32_t antr[3] = { uint32_t(N), uint32_t(W), uint32_t(H) };
    const float aukstis = float(visi.size() * layer_h);
    std::fwrite(antr, sizeof(uint32_t), 3, f);
    std::fwrite(&aukstis, sizeof(float), 1, f);
    for (int k = 0; k < N; ++k) {
        kauke(mo[k], a);
        kauke(k < int(su.size()) ? su[k] : ExPolygons{}, s);
        std::fwrite(a.data(), 1, a.size(), f);
        std::fwrite(s.data(), 1, s.size(), f);
    }
    std::fclose(f);

    char b2[256];
    std::snprintf(b2, sizeof(b2),
        "{\"kiekis\":%d,\"w\":%d,\"h\":%d,\"aukstis\":%.3f,\"sluoksniu_is_viso\":%zu}",
        N, W, H, aukstis, visi.size());
    g_json = b2;
    return g_json.c_str();
}

} // extern "C"

int main(int argc, char **argv)
{
    if (argc < 2) { std::printf("naudojimas: sla.js <model.stl> [sluoksnis] [tree]\n"); return 2; }
    const double layer_h = argc > 2 ? std::atof(argv[2]) : 0.05;
    const std::string t  = argc > 3 ? argv[3] : "regular";
    TriangleMesh mesh;
    if (!mesh.ReadSTLFile(argv[1])) { std::printf("STL neperskaitytas: %s\n", argv[1]); return 3; }
    std::printf("model            %s\n", argv[1]);
    run_chain(mesh, layer_h, t == "tree" || t == "branching", true, true);
    if (argc > 4) std::printf("sl1              %s\n", sla_export_sl1(argv[4], "spaudinys"));
    return 0;
}
