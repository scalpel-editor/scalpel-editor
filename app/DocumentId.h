// Opaque handle for a retained Scintilla document shared by the host, workspace, and tab strip.

#ifndef DOCUMENTID_H
#define DOCUMENTID_H

#include <cstdint>

namespace Scalpel {

/** Opaque handle for a retained Scintilla document. Zero is never valid. */
using DocumentId = uint64_t;

}

#endif
