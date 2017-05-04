/***************************************************************************
 * $Id: instrument.h, v1.0 2010/08/30 SethDart Exp $
 *
 * Project:  OpenCPN
 * Purpose:  Dashboard Plugin
 * Author:   Jean-Eudes Onfray
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
 ***************************************************************************
 */

#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
#endif //precompiled headers

#if !wxUSE_GRAPHICS_CONTEXT
#define wxGCDC wxDC
#endif

// Required GetGlobalColor
#include "../../../include/ocpn_plugin.h"
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>         // supplemental, for Mac

const wxString DEGREE_SIGN = wxString::Format(_T("%c"), 0x00B0); // This is the degree sign in UTF8. It should be correctly handled on both Win & Unix
#define DefaultWidth 150

extern wxFont *g_pFontTitle;
extern wxFont *g_pFontData;
extern wxFont *g_pFontLabel;
extern wxFont *g_pFontSmall;

wxString toSDMM ( int NEflag, double a );

class DashboardInstrument;
class DashboardInstrument_Single;
class DashboardInstrument_Position;
class DashboardInstrument_Sun;

enum : uint64_t
{
    OCPN_DBP_STC_LAT = 1ULL << 0,
    OCPN_DBP_STC_LON = 1ULL << 1,
    OCPN_DBP_STC_SOG = 1ULL << 2,
    OCPN_DBP_STC_COG = 1ULL << 3,
    OCPN_DBP_STC_STW = 1ULL << 4,
    OCPN_DBP_STC_HDM = 1ULL << 5,
    OCPN_DBP_STC_HDT = 1ULL << 6,
    OCPN_DBP_STC_HMV = 1ULL << 7, // Magnetic variation
    OCPN_DBP_STC_BRG = 1ULL << 8,
    OCPN_DBP_STC_AWA = 1ULL << 9,
    OCPN_DBP_STC_AWS = 1ULL << 10,
    OCPN_DBP_STC_TWA = 1ULL << 11,
    OCPN_DBP_STC_TWS = 1ULL << 12,
    OCPN_DBP_STC_DPT = 1ULL << 13,
    OCPN_DBP_STC_TMP = 1ULL << 14,
    OCPN_DBP_STC_VMG = 1ULL << 15,
    OCPN_DBP_STC_RSA = 1ULL << 16,
    OCPN_DBP_STC_SAT = 1ULL << 17,
    OCPN_DBP_STC_GPS = 1ULL << 18,
    OCPN_DBP_STC_PLA = 1ULL << 19, // Cursor latitude
    OCPN_DBP_STC_PLO = 1ULL << 20, // Cursor longitude
    OCPN_DBP_STC_CLK = 1ULL << 21,
    OCPN_DBP_STC_MON = 1ULL << 22,
    OCPN_DBP_STC_ATMP = 1ULL << 23, //AirTemp
    OCPN_DBP_STC_TWD = 1ULL << 24,
    OCPN_DBP_STC_TWS2 = 1ULL << 25,
    OCPN_DBP_STC_VLW1 = 1ULL << 26, // Trip Log
    OCPN_DBP_STC_VLW2 = 1ULL << 27,  // Sum Log
    OCPN_DBP_STC_MDA = 1ULL << 28,  // Bareometic pressure
    OCPN_DBP_STC_MCOG = 1ULL << 29,  // Magnetic Course over Ground
    OCPN_DBP_STC_PITCH = 1ULL << 30, //Pitch
    OCPN_DBP_STC_HEEL = 1ULL << 31,   //Heel 
    OCPN_DBP_STC_LAST = 1ULL << 32
};

class DashboardInstrument : public wxControl
{
public:
      DashboardInstrument(wxWindow *pparent, wxWindowID id, wxString title, uint64_t cap_flag);
      ~DashboardInstrument(){}

      int GetCapacity();
      void OnEraseBackground(wxEraseEvent& WXUNUSED(evt));
      virtual wxSize GetSize( int orient, wxSize hint ) = 0;
      void OnPaint(wxPaintEvent& WXUNUSED(event));
      virtual void SetData(int st, double data, wxString unit) = 0;
      void SetDrawSoloInPane(bool value);
      void MouseEvent( wxMouseEvent &event );
      
      int               instrumentTypeId;

protected:
      uint64_t          m_cap_flag;
      int               m_TitleHeight;
      wxString          m_title;

      virtual void Draw(wxGCDC* dc) = 0;
private:
    bool m_drawSoloInPane;
};

class DashboardInstrument_Single : public DashboardInstrument
{
public:
      DashboardInstrument_Single(wxWindow *pparent, wxWindowID id, wxString title, uint64_t cap, wxString format);
      ~DashboardInstrument_Single(){}

      wxSize GetSize( int orient, wxSize hint );
      void SetData(int st, double data, wxString unit);

protected:
      wxString          m_data;
      wxString          m_format;
      int               m_DataHeight;

      void Draw(wxGCDC* dc);
};

class DashboardInstrument_Position : public DashboardInstrument
{
public:
      DashboardInstrument_Position(wxWindow *pparent, wxWindowID id, wxString title, uint64_t cap_flag1=OCPN_DBP_STC_LAT, uint64_t cap_flag2=OCPN_DBP_STC_LON);
      ~DashboardInstrument_Position(){}

      wxSize GetSize( int orient, wxSize hint );
      void SetData(int st, double data, wxString unit);

protected:
      wxString          m_data1;
      wxString          m_data2;
      uint64_t          m_cap_flag1;
      uint64_t          m_cap_flag2;
      int               m_DataHeight;

      void Draw(wxGCDC* dc);
};

#endif
