#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/Logger.hpp"

namespace vd {

struct VDTraceContext;

struct VDTraceContext {
  VDTraceContext(VDTraceContext* parent): prev{parent}, next{nullptr}
  {
    prev->next = this;
  }

  ~VDTraceContext()
  {
    if(prev) { prev->next = nullptr; }
  }

  VDTraceContext* prev;
  VDTraceContext* next;
};

inline static thread_local VDTraceContext* VD_TRACE_CONTEXT_HEAD =
  nullptr;

#define VD_MARKER_SCOPED()

//#define VD_MARKER_SCOPED()                \
//  {                               \
//    VDLogI("%s\n", __FUNCTION__); \
//  }

#define VD_TRACE(MSG, ...)              \
  VDTraceContext VD_UNIQUE(vdTraceCtx)( \
    VD_TRACE_CONTEXT_HEAD, MSG, __VA_ARGS__);

} // namespace vd