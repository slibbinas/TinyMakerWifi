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

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

using namespace Slic3r;
using Clock = std::chrono::steady_clock;

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

    /* ⚠️ NEISTIRTA (2026-08-19): su „tree" padas iseina TUSCIAS (0 mm3), o
       PrusaSlicer tam paciam puodeliui pirmame sluoksnyje turi ~247 mm2
       pedsaka. Bendra derva sutampa per 1,6 %, tad efektas mazas, bet
       priezastis nezinoma - tikrinti pries siulant „tree" naudotojui. */
    praneskEiga("dedamas raftas", 92);
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
    g_last.model = its;
    g_last.supports = tree.first;
    g_last.pad = pad;
    g_last.layer_h = layer_h;
    g_last.ready = true;

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
    float zmin = 1e30f, zmax = -1e30f;
    for (const indexed_triangle_set *m : {&g_last.model, &g_last.supports, &g_last.pad})
        for (const auto &v : m->vertices) {
            if (v.z() < zmin) zmin = v.z();
            if (v.z() > zmax) zmax = v.z();
        }
    std::vector<float> grid;
    for (float z = zmin + float(layer_h) / 2.f; z < zmax; z += float(layer_h))
        grid.push_back(z);

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

    for (size_t i = 0; i < grid.size(); ++i) {
        if ((i & 63) == 0)
            praneskEiga("gaminami sluoksniai", int(100.0 * i / grid.size()));
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

    float zmin = 1e30f, zmax = -1e30f;
    for (const indexed_triangle_set *m : {&g_last.model, &g_last.supports, &g_last.pad})
        for (const auto &v : m->vertices) {
            if (v.z() < zmin) zmin = v.z();
            if (v.z() > zmax) zmax = v.z();
        }
    std::vector<float> visi;
    for (float z = zmin + float(layer_h) / 2.f; z < zmax; z += float(layer_h))
        visi.push_back(z);
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
