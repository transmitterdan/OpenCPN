#include "iirfilter.h"

#include <wx/math.h>

iirfilter::iirfilter(double fc, filterType tp) {
    wxASSERT(tp == IIRFILTER_TYPE_DEG || tp == IIRFILTER_TYPE_LINEAR || tp == IIRFILTER_TYPE_RAD);
    setFC(fc);
    type = tp;
    reset();
}

double iirfilter::filter(double data) {
    if (!wxIsNaN(data) && !wxIsNaN(b1)) {
        if (wxIsNaN(accum))
            accum = 0.0;
        switch (type) {
            case IIRFILTER_TYPE_LINEAR:
                accum = accum * b1 + a0 * data;
                break;

            case IIRFILTER_TYPE_DEG:
                unwrap(data);
                accum = accum * b1 + a0 * (oldAng + 360.0*wraps);
                break;
                   
            case IIRFILTER_TYPE_RAD:
                unwrap(data);
                accum = accum * b1 + a0 * (oldAng + 2.0*M_PI*wraps);
                break;
                    
            default:
                wxASSERT(false);
        }
    }
    else
        accum = data;
    return get();
}

void iirfilter::reset(double a) {
    accum = a;
    oldAng = NAN;
    wraps = 0;
}

void iirfilter::setFC(double fc) {
    if (wxIsNaN(fc) || fc <= 0.0)
        a0 = b1 = NAN;  // NAN means no filtering will be done
    else {
        reset();
        b1 = exp(-2.0 * 3.1415926535897932384626433832795 * fc);
        a0 = 1.0 - b1;
    }
}

void iirfilter::setType(filterType tp)
{
    wxASSERT(tp == IIRFILTER_TYPE_DEG || tp == IIRFILTER_TYPE_LINEAR || tp == IIRFILTER_TYPE_RAD);
    type = tp;
}

double iirfilter::getFc(void) const
{
    if (wxIsNaN(b1))
        return 0.0;
    double fc = log(b1) / (-2.0 * M_PI);
    return fc;
}

double iirfilter::get(void) const {
    if (wxIsNaN(accum))
        return accum;
    double res = accum;
    switch (type) {
        case IIRFILTER_TYPE_DEG:
            while (res < 0) res += 360.0;
            while (res > 360) res -= 360.0;
            break;
                
        case IIRFILTER_TYPE_RAD:
            while (res < 0) res += 2.0*M_PI;
            while (res > 2.0*M_PI) res -= 2.0*M_PI;
            break;
    }
    return res;
}

void iirfilter::unwrap(double ang) {
    double pi;
    switch ( type ) {
    case IIRFILTER_TYPE_DEG:
        pi = 180;
        break;
    case IIRFILTER_TYPE_RAD:
        pi = M_PI;
        break;
    default:
        return;
    }
    if ( ang - oldAng > pi ) {
            wraps--;
    }
    else if ( ang - oldAng < -pi ) {
        wraps++;
    }
    oldAng = ang;
}
