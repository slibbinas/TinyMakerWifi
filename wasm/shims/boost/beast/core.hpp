/* boost::beast/core pakaitalas: libslic3r ji traukia TIK del base64 (miniatiuru
   koduoti), o tikrasis base64 gyvena atskiroje antrasteje be tinklo. Pilnas
   beast/core tempia boost::asio, kuriam Emscripten nera nei Windows, nei POSIX. */
#pragma once
#include <boost/beast/core/detail/base64.hpp>
