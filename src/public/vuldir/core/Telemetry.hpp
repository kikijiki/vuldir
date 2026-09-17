#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/Logger.hpp"

namespace vd {

struct VDTraceContext;

struct VDTraceContext {
  VDTraceContext(VDTraceContext* parent): prev{parent}, next{nullptr}
  {
    if(prev) prev->next = this;
  }

  ~VDTraceContext()
  {
    if(prev && prev->next == this) { prev->next = nullptr; }
  }

  VDTraceContext* prev;
  VDTraceContext* next;
};

inline static thread_local VDTraceContext* VD_TRACE_CONTEXT_HEAD =
  nullptr;

#define VD_MARKER_SCOPED()

// Tracing is disabled. Arguments are not evaluated.
#define VD_TRACE(...) ((void)0)

} // namespace vd
