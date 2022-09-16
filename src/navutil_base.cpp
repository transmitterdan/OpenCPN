/***************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Navigation Utility Functions without GUI deps
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 **************************************************************************/

/**************************************************************************/
/*          Formats the coordinates to string                             */
/**************************************************************************/


#include <wx/math.h>
#include <wx/string.h>
#include <wx/translation.h>

#include "navutil_base.h"

#include "navutil_base.h"
#include "vector2D.h"

extern int g_iSDMMFormat;
extern int g_iSpeedFormat;
extern int g_iDistanceFormat;

wxString toSDMM(int NEflag, double a, bool hi_precision) {
  wxString s;
  double mpy;
  short neg = 0;
  int d;
  long m;
  double ang = a;
  char c = 'N';

  if (a < 0.0) {
    a = -a;
    neg = 1;
  }
  d = (int)a;
  if (neg) d = -d;
  if (NEflag) {
    if (NEflag == 1) {
      c = 'N';

      if (neg) {
        d = -d;
        c = 'S';
      }
    } else if (NEflag == 2) {
      c = 'E';

      if (neg) {
        d = -d;
        c = 'W';
      }
    }
  }

  switch (g_iSDMMFormat) {
    case 0:
      mpy = 600.0;
      if (hi_precision) mpy = mpy * 1000;

      m = (long)wxRound((a - (double)d) * mpy);

      if (!NEflag || NEflag < 1 || NEflag > 2)  // Does it EVER happen?
      {
        if (hi_precision)
          s.Printf(_T ( "%d%c %02ld.%04ld'" ), d, 0x00B0, m / 10000, m % 10000);
        else
          s.Printf(_T ( "%d%c %02ld.%01ld'" ), d, 0x00B0, m / 10, m % 10);
      } else {
        if (hi_precision)
          if (NEflag == 1)
            s.Printf(_T ( "%02d%c %02ld.%04ld' %c" ), d, 0x00B0, m / 10000,
                     (m % 10000), c);
          else
            s.Printf(_T ( "%03d%c %02ld.%04ld' %c" ), d, 0x00B0, m / 10000,
                     (m % 10000), c);
        else if (NEflag == 1)
          s.Printf(_T ( "%02d%c %02ld.%01ld' %c" ), d, 0x00B0, m / 10, (m % 10), c);
        else
          s.Printf(_T ( "%03d%c %02ld.%01ld' %c" ), d, 0x00B0, m / 10, (m % 10), c);
      }
      break;
    case 1:
      if (hi_precision)
        s.Printf(_T ( "%03.6f" ),
                 ang);  // cca 11 cm - the GPX precision is higher, but as we
                        // use hi_precision almost everywhere it would be a
                        // little too much....
      else
        s.Printf(_T ( "%03.4f" ), ang);  // cca 11m
      break;
    case 2:
      m = (long)((a - (double)d) * 60);
      mpy = 10.0;
      if (hi_precision) mpy = mpy * 100;
      long sec = (long)((a - (double)d - (((double)m) / 60)) * 3600 * mpy);

      if (!NEflag || NEflag < 1 || NEflag > 2)  // Does it EVER happen?
      {
        if (hi_precision)
          s.Printf(_T ( "%d%c %ld'%ld.%ld\"" ), d, 0x00B0, m, sec / 1000,
                   sec % 1000);
        else
          s.Printf(_T ( "%d%c %ld'%ld.%ld\"" ), d, 0x00B0, m, sec / 10, sec % 10);
      } else {
        if (hi_precision)
          if (NEflag == 1)
            s.Printf(_T ( "%02d%c %02ld' %02ld.%03ld\" %c" ), d, 0x00B0, m,
                     sec / 1000, sec % 1000, c);
          else
            s.Printf(_T ( "%03d%c %02ld' %02ld.%03ld\" %c" ), d, 0x00B0, m,
                     sec / 1000, sec % 1000, c);
        else if (NEflag == 1)
          s.Printf(_T ( "%02d%c %02ld' %02ld.%ld\" %c" ), d, 0x00B0, m, sec / 10,
                   sec % 10, c);
        else
          s.Printf(_T ( "%03d%c %02ld' %02ld.%ld\" %c" ), d, 0x00B0, m, sec / 10,
                   sec % 10, c);
      }
      break;
  }
  return s;
}

/**************************************************************************/
/*          Converts the speed to the units selected by user              */
/**************************************************************************/
double toUsrSpeed(double kts_speed, int unit) {
  double ret = NAN;
  if (unit == -1) unit = g_iSpeedFormat;
  switch (unit) {
    case SPEED_KTS:  // kts
      ret = kts_speed;
      break;
    case SPEED_MPH:  // mph
      ret = kts_speed * 1.15078;
      break;
    case SPEED_KMH:  // km/h
      ret = kts_speed * 1.852;
      break;
    case SPEED_MS:  // m/s
      ret = kts_speed * 0.514444444;
      break;
  }
  return ret;
}

/**************************************************************************/
/*          Converts the distance to the units selected by user           */
/**************************************************************************/
double toUsrDistance(double nm_distance, int unit) {
  double ret = NAN;
  if (unit == -1) unit = g_iDistanceFormat;
  switch (unit) {
    case DISTANCE_NMI:  // Nautical miles
      ret = nm_distance;
      break;
    case DISTANCE_MI:  // Statute miles
      ret = nm_distance * 1.15078;
      break;
    case DISTANCE_KM:
      ret = nm_distance * 1.852;
      break;
    case DISTANCE_M:
      ret = nm_distance * 1852;
      break;
    case DISTANCE_FT:
      ret = nm_distance * 6076.12;
      break;
    case DISTANCE_FA:
      ret = nm_distance * 1012.68591;
      break;
    case DISTANCE_IN:
      ret = nm_distance * 72913.4;
      break;
    case DISTANCE_CM:
      ret = nm_distance * 185200;
      break;
  }
  return ret;
}


/**************************************************************************/
/*          Returns the abbreviation of user selected distance unit       */
/**************************************************************************/
wxString getUsrDistanceUnit(int unit) {
  wxString ret;
  if (unit == -1) unit = g_iDistanceFormat;
  switch (unit) {
    case DISTANCE_NMI:  // Nautical miles
      ret = _("NMi");
      break;
    case DISTANCE_MI:  // Statute miles
      ret = _("mi");
      break;
    case DISTANCE_KM:
      ret = _("km");
      break;
    case DISTANCE_M:
      ret = _("m");
      break;
    case DISTANCE_FT:
      ret = _("ft");
      break;
    case DISTANCE_FA:
      ret = _("fa");
      break;
    case DISTANCE_IN:
      ret = _("in");
      break;
    case DISTANCE_CM:
      ret = _("cm");
      break;
  }
  return ret;
}


/**************************************************************************/
/*          Returns the abbreviation of user selected speed unit          */
/**************************************************************************/
wxString getUsrSpeedUnit(int unit) {
  wxString ret;
  if (unit == -1) unit = g_iSpeedFormat;
  switch (unit) {
    case SPEED_KTS:  // kts
      ret = _("kts");
      break;
    case SPEED_MPH:  // mph
      ret = _("mph");
      break;
    case SPEED_KMH:
      ret = _("km/h");
      break;
    case SPEED_MS:
      ret = _("m/s");
      break;
  }
  return ret;
}

wxString FormatDistanceAdaptive(double distance) {
  wxString result;
  int unit = g_iDistanceFormat;
  double usrDistance = toUsrDistance(distance, unit);
  if (usrDistance < 0.1 &&
      (unit == DISTANCE_KM || unit == DISTANCE_MI || unit == DISTANCE_NMI)) {
    unit = (unit == DISTANCE_MI) ? DISTANCE_FT : DISTANCE_M;
    usrDistance = toUsrDistance(distance, unit);
  }
  wxString format;
  if (usrDistance < 5.0) {
    format = _T("%1.2f ");
  } else if (usrDistance < 100.0) {
    format = _T("%2.1f ");
  } else if (usrDistance < 1000.0) {
    format = _T("%3.0f ");
  } else {
    format = _T("%4.0f ");
  }
  result << wxString::Format(format, usrDistance) << getUsrDistanceUnit(unit);
  return result;
}

/**************************************************************************/
/*          Converts the speed from the units selected by user to knots   */
/**************************************************************************/
double fromUsrSpeed(double usr_speed, int unit, int default_val) {
  double ret = NAN;

  if (unit == -1) unit = default_val;
  switch (unit) {
    case SPEED_KTS:  // kts
      ret = usr_speed;
      break;
    case SPEED_MPH:  // mph
      ret = usr_speed / 1.15078;
      break;
    case SPEED_KMH:  // km/h
      ret = usr_speed / 1.852;
      break;
    case SPEED_MS:  // m/s
      ret = usr_speed / 0.514444444;
      break;
  }
  return ret;
}

/**************************************************************************/
/*          Converts the distance from the units selected by user to NMi  */
/**************************************************************************/
double fromUsrDistance(double usr_distance, int unit, int default_val) {
  double ret = NAN;
  if (unit == -1) unit = default_val;
  switch (unit) {
    case DISTANCE_NMI:  // Nautical miles
      ret = usr_distance;
      break;
    case DISTANCE_MI:  // Statute miles
      ret = usr_distance / 1.15078;
      break;
    case DISTANCE_KM:
      ret = usr_distance / 1.852;
      break;
    case DISTANCE_M:
      ret = usr_distance / 1852;
      break;
    case DISTANCE_FT:
      ret = usr_distance / 6076.12;
      break;
  }
  return ret;
}

//---------------------------------------------------------------------------------
//          Vector Stuff for Hit Test Algorithm
//---------------------------------------------------------------------------------
double vGetLengthOfNormal(pVector2D a, pVector2D b, pVector2D n) {
  vector2D c, vNormal;
  vNormal.x = 0;
  vNormal.y = 0;
  //
  // Obtain projection vector.
  //
  // c = ((a * b)/(|b|^2))*b
  //
  c.x = b->x * (vDotProduct(a, b) / vDotProduct(b, b));
  c.y = b->y * (vDotProduct(a, b) / vDotProduct(b, b));
  //
  // Obtain perpendicular projection : e = a - c
  //
  vSubtractVectors(a, &c, &vNormal);
  //
  // Fill PROJECTION structure with appropriate values.
  //
  *n = vNormal;

  return (vVectorMagnitude(&vNormal));
}

double vDotProduct(pVector2D v0, pVector2D v1) {
  double dotprod;

  dotprod =
      (v0 == NULL || v1 == NULL) ? 0.0 : (v0->x * v1->x) + (v0->y * v1->y);

  return (dotprod);
}

pVector2D vAddVectors(pVector2D v0, pVector2D v1, pVector2D v) {
  if (v0 == NULL || v1 == NULL)
    v = (pVector2D)NULL;
  else {
    v->x = v0->x + v1->x;
    v->y = v0->y + v1->y;
  }
  return (v);
}

pVector2D vSubtractVectors(pVector2D v0, pVector2D v1, pVector2D v) {
  if (v0 == NULL || v1 == NULL)
    v = (pVector2D)NULL;
  else {
    v->x = v0->x - v1->x;
    v->y = v0->y - v1->y;
  }
  return (v);
}

double vVectorSquared(pVector2D v0) {
  double dS;

  if (v0 == NULL)
    dS = 0.0;
  else
    dS = ((v0->x * v0->x) + (v0->y * v0->y));
  return (dS);
}

double vVectorMagnitude(pVector2D v0) {
  double dMagnitude;

  if (v0 == NULL)
    dMagnitude = 0.0;
  else
    dMagnitude = sqrt(vVectorSquared(v0));
  return (dMagnitude);
}


// This function parses a string containing a GPX time representation
// and returns a wxDateTime containing the UTC corresponding to the
// input. The function return value is a pointer past the last valid
// character parsed (if successful) or NULL (if the string is invalid).
//
// Valid GPX time strings are in ISO 8601 format as follows:
//
//   [-]<YYYY>-<MM>-<DD>T<hh>:<mm>:<ss>Z|(+|-<hh>:<mm>)
//
// For example, 2010-10-30T14:34:56Z and 2010-10-30T14:34:56-04:00
// are the same time. The first is UTC and the second is EDT.

const wxChar *ParseGPXDateTime(wxDateTime &dt, const wxChar *datetime) {
  long sign, hrs_west, mins_west;
  const wxChar *end;

  // Skip any leading whitespace
  while (isspace(*datetime)) datetime++;

  // Skip (and ignore) leading hyphen
  if (*datetime == wxT('-')) datetime++;

  // Parse and validate ISO 8601 date/time string
  if ((end = dt.ParseFormat(datetime, wxT("%Y-%m-%dT%T"))) != NULL) {
    // Invalid date/time
    if (*end == 0) return NULL;

    // ParseFormat outputs in UTC if the controlling
    // wxDateTime class instance has not been initialized.

    // Date/time followed by UTC time zone flag, so we are done
    else if (*end == wxT('Z')) {
      end++;
      return end;
    }

    // Date/time followed by given number of hrs/mins west of UTC
    else if (*end == wxT('+') || *end == wxT('-')) {
      // Save direction from UTC
      if (*end == wxT('+'))
        sign = 1;
      else
        sign = -1;
      end++;

      // Parse hrs west of UTC
      if (isdigit(*end) && isdigit(*(end + 1)) && *(end + 2) == wxT(':')) {
        // Extract and validate hrs west of UTC
        wxString(end).ToLong(&hrs_west);
        if (hrs_west > 12) return NULL;
        end += 3;

        // Parse mins west of UTC
        if (isdigit(*end) && isdigit(*(end + 1))) {
          // Extract and validate mins west of UTC
          wxChar mins[3];
          mins[0] = *end;
          mins[1] = *(end + 1);
          mins[2] = 0;
          wxString(mins).ToLong(&mins_west);
          if (mins_west > 59) return NULL;

          // Apply correction
          dt -= sign * wxTimeSpan(hrs_west, mins_west, 0, 0);
          return end + 2;
        } else
          // Missing mins digits
          return NULL;
      } else
        // Missing hrs digits or colon
        return NULL;
    } else
      // Unknown field after date/time (not UTC, not hrs/mins
      //  west of UTC)
      return NULL;
  } else
    // Invalid ISO 8601 date/time
    return NULL;
}


