/* boost::log pakaitalas WASM'ui.
 *
 * libslic3r zurnala rasо per BOOST_LOG_TRIVIAL(...) << ...; narsykleje to
 * nereikia, o pati boost::log yra KOMPILIUOJAMA boost dalis, kurios Emscripten
 * portas (tik antrastes) neturi. Cia makro virsta tuscia priimtuvu - eilutes
 * kodekode lieka nepaliestos, tik niekur nekeliauja.
 */
#pragma once
#include <ostream>

namespace boost { namespace log { namespace trivial {
enum severity_level { trace, debug, info, warning, error, fatal };
struct severity_tag {};
inline severity_tag severity;
struct filter_expr { bool operator()(...) const { return false; } };
inline filter_expr operator>=(severity_tag, severity_level) { return {}; }
inline filter_expr operator<=(severity_tag, severity_level) { return {}; }
inline filter_expr operator==(severity_tag, severity_level) { return {}; }
}}}

namespace slic3r_wasm_log {
struct NullSink {
    template<class T> NullSink &operator<<(const T &) { return *this; }
    NullSink &operator<<(std::ostream &(*)(std::ostream &)) { return *this; }
};
}

namespace boost { namespace log {
struct core {
    static core *get() { static core c; return &c; }
    void set_filter(...) {}
    void reset_filter() {}
    void set_logging_enabled(bool = true) {}
    void remove_all_sinks() {}
};
namespace v2s_st = ::boost::log;
}}

#define BOOST_LOG_TRIVIAL(lvl) ::slic3r_wasm_log::NullSink()
