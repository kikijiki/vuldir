#pragma once

#include "vuldir/core/Math.hpp"

namespace vd::sample {

class MouseDeltaTracker
{
public:
  Int2 Update(i32 x, i32 y)
  {
    const Int2 delta = m_hasPosition ? Int2{x - m_x, m_y - y} : Int2{};
    m_x              = x;
    m_y              = y;
    m_hasPosition    = true;
    return delta;
  }

private:
  i32  m_x           = 0;
  i32  m_y           = 0;
  bool m_hasPosition = false;
};

} // namespace vd::sample
