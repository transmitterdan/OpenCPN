
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif  // precompiled headers

#include "iirfilter.h"
#include <cmath>

#include <wx/math.h>

iirfilter::iirfilter(double fc, IIRfilter_type tp) {
  wxASSERT(tp == IIRFILTER_TYPE_DEG || tp == IIRFILTER_TYPE_LINEAR ||
    tp == IIRFILTER_TYPE_RAD);
  b1 = 1.0;
  a0 = 0;
  setFC(fc);
  type = tp;
  reset();
  accum = NAN;
}

double iirfilter::filter(double data) {
  if (!std::isnan(data) && !std::isnan(b1) && !std::isnan(accum)) {
    switch (type) {
      case IIRFILTER_TYPE_LINEAR:
        accum = accum * b1 + a0 * data;
        break;

      case IIRFILTER_TYPE_DEG:
        unwrapDeg(data);
        accum = accum * b1 + a0 * (oldDeg + 360.0 * wraps);
        break;

      case IIRFILTER_TYPE_RAD:
        unwrapRad(data);
        accum = accum * b1 + a0 * (oldRad + 2.0 * M_PI * wraps);
        break;

      default:
        wxASSERT(false);
    }
  } else
    accum = data;
  return get();
}

void iirfilter::reset(double a) {
  accum = a;
  oldDeg = NAN;
  oldRad = NAN;
  wraps = 0;
}

void iirfilter::setFC(double fc) {
  if (abs(fc - getFC()) > 1e-9) {
    if (std::isnan(fc) || fc <= 0.0)
      a0 = b1 = NAN;  // NAN means no filtering will be done
    else {
      reset(accum);
      b1 = exp(-2.0 * 3.1415926535897932384626433832795 * fc);
      a0 = 1 - b1;
    }
  }
}

void iirfilter::setType(IIRfilter_type tp) {
  wxASSERT_MSG(tp == IIRFILTER_TYPE_DEG || tp == IIRFILTER_TYPE_LINEAR ||
                   tp == IIRFILTER_TYPE_RAD,
               _T("Unknown filter type."));
  type = tp;
}

double iirfilter::getFC(void) const {
  if (std::isnan(b1)) return 0.0;
  double fc = log(b1) / (-2.0 * 3.1415926535897932384626433832795);
  return fc;
}

IIRfilter_type iirfilter::getType(void) const { return type; }

double iirfilter::get(void) const {
  if (std::isnan(accum)) return accum;
  double res = accum;
  wxASSERT_MSG((abs(wraps) < 10), "Too many wraps.");
  switch (type) {
    case IIRFILTER_TYPE_DEG:
      while (res < -180) res += 360.0;
      while (res > 180) res -= 360.0;
      break;

    case IIRFILTER_TYPE_RAD:
      while (res < -M_PI) res += 2.0 * M_PI;
      while (res > M_PI) res -= 2.0 * M_PI;
      break;
  }
  return res;
}

void iirfilter::unwrapDeg(double deg) {
  if (deg - oldDeg > 180) {
    wraps--;
  } else if (deg - oldDeg < -180) {
    wraps++;
  }
  oldDeg = deg;
}

void iirfilter::unwrapRad(double rad) {
  if (rad - oldRad > M_PI) {
    wraps--;
  } else if (rad - oldRad < M_PI) {
    wraps++;
  }
  oldRad = rad;
}