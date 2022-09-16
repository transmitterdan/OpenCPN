/***************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  NMEA Data Multiplexer Object
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

#ifdef __MSVC__
#include "winsock2.h"
#include "wx/msw/winundef.h"
#endif

#include "config.h"

#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif

#if defined(HAVE_READLINK) && !defined(HAVE_LIBGEN_H)
#error Using readlink(3) requires libgen.h which cannot be found.
#endif

#include "wx/wx.h"
#include <wx/tokenzr.h>
//#include "config.h"
#include "comm_n0183_output.h"
//#include "navutil.h"
//#include "NMEALogWindow.h"
#include "Route.h"

#ifdef __linux__
#include "udev_rule_mgr.h"
#endif

#include "gui_lib.h"
#include "SendToGpsDlg.h"

#ifdef USE_GARMINHOST
#include "garmin_wrapper.h"
#endif
#include "wx/jsonval.h"
#include "wx/jsonwriter.h"
#include "wx/jsonreader.h"
#include "nmea0183.h"
#include "conn_params.h"
#include "comm_util.h"
#include "comm_driver.h"
#include "comm_drv_registry.h"
#include "comm_drv_n0183_serial.h"
#include "comm_drv_factory.h"

//extern PlugInManager *g_pi_manager;
extern wxString g_GPS_Ident;
extern bool g_bGarminHostUpload;
extern bool g_bWplUsePosition;
extern wxArrayOfConnPrm *g_pConnectionParams;
extern bool g_b_legacy_input_filter_behaviour;
extern int g_maxWPNameLength;
extern wxString g_TalkerIdText;

#ifdef HAVE_READLINK

static std::string do_readlink(const char *link) {
  // Strip possible Serial: or Usb: prefix:
  const char *colon = strchr(link, ':');
  const char *path = colon ? colon + 1 : link;

  char target[PATH_MAX + 1] = {0};
  int r = readlink(path, target, sizeof(target));
  if (r == -1 && (errno == EINVAL || errno == ENOENT)) {
    // Not a a symlink
    return path;
  }
  if (r == -1) {
    wxLogDebug("Error reading device link %s: %s", path, strerror(errno));
    return path;
  }
  if (*target == '/') {
    return target;
  }
  char buff[PATH_MAX + 1];
  memcpy(buff, path, std::min(strlen(path) + 1, (size_t)PATH_MAX));
  return std::string(dirname(buff)) + "/" + target;
}

static bool is_same_device(const char *port1, const char *port2) {
  std::string dev1 = do_readlink(port1);
  std::string dev2 = do_readlink(port2);
  return dev1 == dev2;
}

#else  // HAVE_READLINK

static bool inline is_same_device(const char *port1, const char *port2) {
  return strcmp(port1, port2) == 0;
}

#endif  // HAVE_READLINK


//void Multiplexer::AddStream(DataStream *stream) { m_pdatastreams->Add(stream); }

// void Multiplexer::StopAllStreams() {
//   for (size_t i = 0; i < m_pdatastreams->Count(); i++) {
//     m_pdatastreams->Item(i)->Close();
//   }
// }

// void Multiplexer::ClearStreams() {
//   for (size_t i = 0; i < m_pdatastreams->Count(); i++) {
//     delete m_pdatastreams->Item(i);  // Implicit Close(), see datastream dtor
//   }
//   m_pdatastreams->Clear();
// }


// DataStream *Multiplexer::FindStream(const wxString &port) {
//   for (size_t i = 0; i < m_pdatastreams->Count(); i++) {
//     DataStream *stream = m_pdatastreams->Item(i);
//     if (stream && stream->GetConnectionType() == INTERNAL_BT) {
//       if (is_same_device(stream->GetPort(), port.AfterFirst(';')))
//         return stream;
//     } else if (stream && is_same_device(stream->GetPort(), port))
//       return stream;
//   }
//   return NULL;
// }

// void Multiplexer::StopAndRemoveStream(DataStream *stream) {
//   if (stream) stream->Close();
//
//   if (m_pdatastreams) {
//     int index = m_pdatastreams->Index(stream);
//     if (wxNOT_FOUND != index) m_pdatastreams->RemoveAt(index);
//   }
// }

// void Multiplexer::StartAllStreams(void) {
// // FIXME (dave) Verify by test
// #if 0
//   for (size_t i = 0; i < g_pConnectionParams->Count(); i++) {
//     ConnectionParams *cp = g_pConnectionParams->Item(i);
//     if (cp->bEnabled) {
// #if defined(__linux__) && !defined(__OCPN__ANDROID__)
//       if (cp->GetDSPort().Contains(_T("Serial"))) {
//         CheckSerialAccess(0, cp->Port.ToStdString());
//       }
// #endif
//       AddStream(makeDataStream(this, cp));
//       cp->b_IsSetup = true;
//     }
//   }
// #endif
// }



void SendNMEAMessage(const wxString &msg) {
//FIXME (dave) Implement using comm...
#if 0
  // Send to all the outputs
  for (size_t i = 0; i < m_pdatastreams->Count(); i++) {
    DataStream *s = m_pdatastreams->Item(i);

    if (s->IsOk() && (s->GetIoSelect() == DS_TYPE_INPUT_OUTPUT ||
                      s->GetIoSelect() == DS_TYPE_OUTPUT)) {
      bool bout_filter = true;

      bool bxmit_ok = true;
      if (s->SentencePassesFilter(msg, FILTER_OUTPUT)) {
        bxmit_ok = s->SendSentence(msg);
        bout_filter = false;
      }
      // Send to the Debug Window, if open
      if (!bout_filter) {
        if (bxmit_ok)
          LogOutputMessageColor(msg, s->GetPort(), _T("<BLUE>"));
        else
          LogOutputMessageColor(msg, s->GetPort(), _T("<RED>"));
      } else
        LogOutputMessageColor(msg, s->GetPort(), _T("<CORAL>"));
    }
  }
  // Send to plugins
  if (g_pi_manager) g_pi_manager->SendNMEASentenceToAllPlugIns(msg);
#endif
}

// void Multiplexer::SetAISHandler(wxEvtHandler *handler) {
//   m_aisconsumer = handler;
// }
//
// void Multiplexer::SetGPSHandler(wxEvtHandler *handler) {
//   m_gpsconsumer = handler;
// }

#if 0
void Multiplexer::OnEvtStream(OCPN_DataStreamEvent &event) {
  wxString message = event.ProcessNMEA4Tags();
  std::string goodMessage(message);
  OCPN_DataStreamEvent *goodEvent = static_cast <OCPN_DataStreamEvent*>(event.Clone());
  bool checksumOK = CheckSumCheck(event.GetNMEAString());
  DataStream *stream = event.GetStream();

  if (!checksumOK) {
    goodMessage = stream->FixChecksum(goodMessage);
    goodEvent->SetNMEAString(goodMessage);
  }

  wxString port(_T("Virtual:"));
  if (stream) port = wxString(stream->GetPort());

  if (!message.IsEmpty()) {
    // Send to core consumers
    // if it passes the source's input filter
    //  If there is no datastream, as for PlugIns, then pass everything
    bool bpass = true;
#if 0
    //FIXME Done
    if (stream) bpass = stream->SentencePassesFilter(message, FILTER_INPUT);

    if (bpass) {
      if (message.Mid(3, 3).IsSameAs(_T("VDM")) ||
          message.Mid(1, 5).IsSameAs(_T("FRPOS")) ||
          message.Mid(1, 4).IsSameAs(_T("CDDS")) ||
          message.Mid(3, 3).IsSameAs(_T("TLL")) ||
          message.Mid(3, 3).IsSameAs(_T("TTM")) ||
          message.Mid(3, 3).IsSameAs(_T("OSD")) ||
          (g_bWplUsePosition && message.Mid(3, 3).IsSameAs(_T("WPL")))) {
        if (m_aisconsumer) m_aisconsumer->AddPendingEvent(*goodEvent);
      } else {
        if (m_gpsconsumer) m_gpsconsumer->AddPendingEvent(*goodEvent);
      }
    }
#endif

    // Send to the Debug Window, if open
    //  Special formatting for non-printable characters helps debugging NMEA
    //  problems
    if (NMEALogWindow::Get().Active()) {
      std::string str = event.GetNMEAString();
      wxString fmsg;

      bool b_error = false;
      for (std::string::iterator it = str.begin(); it != str.end(); ++it) {
        if (isprint(*it))
          fmsg += *it;
        else {
          wxString bin_print;
          bin_print.Printf(_T("<0x%02X>"), *it);
          fmsg += bin_print;
          if ((*it != 0x0a) && (*it != 0x0d)) b_error = true;
        }
      }
      LogInputMessage(fmsg, port, !bpass, b_error);
    }

    if ((g_b_legacy_input_filter_behaviour && !bpass) || bpass) {
#if 0
      //FIXME  Done
      // Send to plugins
      if (g_pi_manager)
        g_pi_manager->SendNMEASentenceToAllPlugIns(goodMessage);
#endif

      //FIXME  TODO
      // Send to all the other outputs
      for (size_t i = 0; i < m_pdatastreams->Count(); i++) {
        DataStream *s = m_pdatastreams->Item(i);
        if (s->IsOk()) {
          if ((s->GetConnectionType() == SERIAL) || (s->GetPort() != port)) {
            if (s->GetIoSelect() == DS_TYPE_INPUT_OUTPUT ||
                s->GetIoSelect() == DS_TYPE_OUTPUT) {
              bool bout_filter = true;

              bool bxmit_ok = true;
              if (s->SentencePassesFilter(message, FILTER_OUTPUT)) {
                bxmit_ok = s->SendSentence(message);
                bout_filter = false;
              }

              // Send to the Debug Window, if open
              if (!bout_filter) {
                if (bxmit_ok)
                  LogOutputMessageColor(message, s->GetPort(), _T("<BLUE>"));
                else
                  LogOutputMessageColor(message, s->GetPort(), _T("<RED>"));
              } else
                LogOutputMessageColor(message, s->GetPort(), _T("<CORAL>"));
            }
          }
        }
      }
    }
  }
  delete goodEvent;
}
#endif


// void Multiplexer::SaveStreamProperties(DataStream *stream) {
//   //FIXME (dave) Implement using comm...
// #if 0
//   if (stream) {
//     wxLogMessage(
//         wxString::Format(_T("SaveStreamProperties %s"),
//                          stream->GetConnectionParams()->GetDSPort().c_str()));
//     params_save = stream->GetConnectionParams();
//   }
// #endif
// }

// bool Multiplexer::CreateAndRestoreSavedStreamProperties() {
//   //FIXME (dave) Implement using comm...
// #if 0
//   wxLogMessage(wxString::Format(_T("CreateAndRestoreSavedStreamProperties %s"),
//                                 params_save->GetDSPort().c_str()));
//   AddStream(makeDataStream(this, params_save));
// #endif
//   return true;
// }

int SendRouteToGPS_N0183(Route *pr, const wxString &com_name,
                                bool bsend_waypoints/*, SendToGpsDlg *dialog*/) {
  int ret_val = 0;
  //FIXME (dave)  Implement using comm
#if 0

  if (g_GPS_Ident == _T("FurunoGP3X")) {
    if (pr->pRoutePointList->GetCount() > 30) {
      long style = wxOK;
      auto dlg = new OCPN_TimedHTMLMessageDialog(
          0,
          _T("Routes containing more than 30 waypoints must be split before ")
          _T("uploading."),
          _("Route Upload"), 10, style, false, wxDefaultPosition);
      int reply = dlg->ShowModal();
      return 1;
    }
  }

  bool b_restoreStream = false;
  bool btempStream = false;
  DataStream *dstr = NULL;
  DataStream *old_stream = NULL;

  if (com_name.Lower().StartsWith("serial")) {
    old_stream = FindStream(com_name);
    wxLogDebug("Looking for old stream %s, %d", com_name, old_stream ? 1 : 0);
    if (old_stream) {
      SaveStreamProperties(old_stream);
      StopAndRemoveStream(old_stream);
      b_restoreStream = true;
    }
  } else {
    dstr = FindStream(com_name);
  }

#ifdef USE_GARMINHOST
#ifdef __WXMSW__
  if (com_name.Upper().Matches(_T("*GARMIN*")))  // Garmin USB Mode
  {
    //        if(m_pdevmon)
    //            m_pdevmon->StopIOThread(true);

    int v_init = Garmin_GPS_Init(wxString(_T("usb:")));

    if (v_init < 0) {
      wxString msg(_T(" Garmin USB GPS could not be initialized"));
      wxLogMessage(msg);
      msg.Printf(_T(" Error Code is %d"), v_init);
      wxLogMessage(msg);
      msg = _T(" LastGarminError is: ");
      msg += GetLastGarminError();
      wxLogMessage(msg);

      ret_val = ERR_GARMIN_INITIALIZE;
    } else {
      wxLogMessage(_T("Garmin USB Initialized"));

      wxString msg = _T("USB Unit identifies as: ");
      wxString GPS_Unit = Garmin_GPS_GetSaveString();
      msg += GPS_Unit;
      wxLogMessage(msg);

      wxLogMessage(_T("Sending Routes..."));
      int ret1 = Garmin_GPS_SendRoute(wxString(_T("usb:")), pr,
                                      dialog->GetProgressGauge());

      if (ret1 != 1) {
        wxLogMessage(_T(" Error Sending Routes"));
        wxString msg;
        msg = _T(" LastGarminError is: ");
        msg += GetLastGarminError();
        wxLogMessage(msg);

        ret_val = ERR_GARMIN_GENERAL;
      } else
        ret_val = 0;
    }

    //        if(m_pdevmon)
    //            m_pdevmon->RestartIOThread();

    goto ret_point_1;
  }
#endif

  if (g_bGarminHostUpload) {
    int lret_val;
    if (dialog && dialog->GetProgressGauge()) {
      dialog->GetProgressGauge()->SetValue(20);
      dialog->GetProgressGauge()->Refresh();
      dialog->GetProgressGauge()->Update();
    }

    wxString short_com = com_name.Mid(7);
    // Initialize the Garmin receiver, build required Jeeps internal data
    // structures
    int v_init = Garmin_GPS_Init(short_com);
    if (v_init < 0) {
      wxString msg(_T("Garmin GPS could not be initialized on port: "));
      msg += short_com;
      wxString err;
      err.Printf(_T(" Error Code is %d"), v_init);
      msg += err;

      msg += _T("\n LastGarminError is: ");
      msg += GetLastGarminError();

      wxLogMessage(msg);

      ret_val = ERR_GARMIN_INITIALIZE;
      goto ret_point;
    } else {
      wxString msg(_T("Sent Route to Garmin GPS on port: "));
      msg += short_com;
      msg += _T("\n Unit identifies as: ");
      wxString GPS_Unit = Garmin_GPS_GetSaveString();
      msg += GPS_Unit;

      wxLogMessage(msg);
    }

    if (dialog && dialog->GetProgressGauge()) {
      dialog->GetProgressGauge()->SetValue(40);
      dialog->GetProgressGauge()->Refresh();
      dialog->GetProgressGauge()->Update();
    }

    lret_val = Garmin_GPS_SendRoute(short_com, pr, dialog->GetProgressGauge());
    if (lret_val != 1) {
      wxString msg(_T("Error Sending Route to Garmin GPS on port: "));
      msg += short_com;
      wxString err;
      err.Printf(_T(" Error Code is %d"), ret_val);

      msg += _T("\n LastGarminError is: ");
      msg += GetLastGarminError();

      msg += err;
      wxLogMessage(msg);

      ret_val = ERR_GARMIN_GENERAL;
      goto ret_point;
    } else
      ret_val = 0;

  ret_point:

    if (dialog && dialog->GetProgressGauge()) {
      dialog->GetProgressGauge()->SetValue(100);
      dialog->GetProgressGauge()->Refresh();
      dialog->GetProgressGauge()->Update();
    }

    wxMilliSleep(500);

    goto ret_point_1;
  } else
#endif  // USE_GARMINHOST

  {

    {  // Standard NMEA mode

      if (com_name.Lower().StartsWith("serial")) {
        //  If the port was temporarily closed, reopen as I/O type
        //  Otherwise, open another port using default properties
        wxString baud;

        if (old_stream) {
          baud = wxString::Format(wxT("%i"), params_save->Baudrate);
        } else {
          baud = _T("4800");
        }

        dstr = makeSerialDataStream(this, SERIAL, com_name, baud,
                                    DS_TYPE_INPUT_OUTPUT, 0, false);
        btempStream = true;

#ifdef __OCPN__ANDROID__
        wxMilliSleep(1000);
#else
        //  Wait up to 5 seconds for Datastream secondary thread to come up
        int timeout = 0;
        while (!dstr->IsSecThreadActive() && (timeout < 50)) {
          wxMilliSleep(100);
          timeout++;
        }

        if (!dstr->IsSecThreadActive()) {
          wxString msg(_T("-->GPS Port:"));
          msg += com_name;
          msg += _T(" ...Could not be opened for writing");
          wxLogMessage(msg);

          dstr->Close();
          goto ret_point_1;
        }
#endif
      } else if (com_name.Find("Bluetooth") != wxNOT_FOUND) {
        if (dstr == NULL) {
          ConnectionParams *pConnectionParams = new ConnectionParams();
          pConnectionParams->Type = INTERNAL_BT;
          wxStringTokenizer tkz(com_name, _T(";"));
          wxString name = tkz.GetNextToken();
          wxString mac = tkz.GetNextToken();

          pConnectionParams->NetworkAddress = name;
          pConnectionParams->Port = mac;
          pConnectionParams->NetworkPort = 0;
          pConnectionParams->NetProtocol = PROTO_UNDEFINED;
          pConnectionParams->Baudrate = 0;

          dstr = makeDataStream(this, pConnectionParams);

          btempStream = true;
        }
      }

      else if (com_name.Lower().StartsWith("udp") ||
               com_name.Lower().StartsWith("tcp")) {
        if (dstr == NULL) {
          NetworkProtocol protocol = UDP;
          if (com_name.Lower().StartsWith("tcp")) protocol = TCP;
          wxStringTokenizer tkz(com_name, _T(":"));
          wxString token = tkz.GetNextToken();
          wxString address = tkz.GetNextToken();
          token = tkz.GetNextToken();
          long port;
          token.ToLong(&port);

          NetworkDataStream *streamNew =
              new NetworkDataStream(this, protocol, address, port);
          if (streamNew->IsOk()) {
            dstr = (DataStream *)streamNew;
          }
          btempStream = true;
        }
      }

      if (com_name.Lower().StartsWith("tcp")) {
        // new tcp connections must wait for connect
        wxString msg = _("Connecting to ");
        msg += com_name;
        dialog->SetMessage(msg);
        dialog->GetProgressGauge()->Pulse();

        NetworkDataStream *streamTest = (NetworkDataStream *)dstr;
        int loopCount = 10;  // seconds
        bool bconnected = false;
        while (!bconnected && (loopCount > 0)) {
          if (streamTest->GetSock()) {
            if (streamTest->GetSock()->IsConnected()) {
              bconnected = true;
              break;
            }
          }
          dialog->GetProgressGauge()->Pulse();
          wxYield();
          wxSleep(1);
          loopCount--;
        }
        if (bconnected) {
          msg = _("Connected to ");
          msg += com_name;
          dialog->SetMessage(msg);
        } else {
          if (btempStream) {
            dstr->Close();
            delete dstr;
          }
          return 1;
        }
      }

      SENTENCE snt;
      NMEA0183 oNMEA0183;
      oNMEA0183.TalkerID = _T ( "EC" );

      int nProg = pr->pRoutePointList->GetCount() + 1;
      if (dialog && dialog->GetProgressGauge())
        dialog->GetProgressGauge()->SetRange(100);

      int progress_stall = 500;
      if (pr->pRoutePointList->GetCount() > 10) progress_stall = 200;

      if (!dialog) progress_stall = 200;  // 80 chars at 4800 baud is ~160 msec

      // Send out the waypoints, in order
      if (bsend_waypoints) {
        wxRoutePointListNode *node = pr->pRoutePointList->GetFirst();

        int ip = 1;
        while (node) {
          RoutePoint *prp = node->GetData();

          if (g_GPS_Ident == _T("Generic")) {
            if (prp->m_lat < 0.)
              oNMEA0183.Wpl.Position.Latitude.Set(-prp->m_lat, _T ( "S" ));
            else
              oNMEA0183.Wpl.Position.Latitude.Set(prp->m_lat, _T ( "N" ));

            if (prp->m_lon < 0.)
              oNMEA0183.Wpl.Position.Longitude.Set(-prp->m_lon, _T ( "W" ));
            else
              oNMEA0183.Wpl.Position.Longitude.Set(prp->m_lon, _T ( "E" ));

            oNMEA0183.Wpl.To = prp->GetName().Truncate(g_maxWPNameLength);

            oNMEA0183.Wpl.Write(snt);

          } else if (g_GPS_Ident == _T("FurunoGP3X")) {
            //  Furuno has its own talker ID, so do not allow the global
            //  override
            wxString talker_save = g_TalkerIdText;
            g_TalkerIdText.Clear();

            oNMEA0183.TalkerID = _T ( "PFEC," );

            if (prp->m_lat < 0.)
              oNMEA0183.GPwpl.Position.Latitude.Set(-prp->m_lat, _T ( "S" ));
            else
              oNMEA0183.GPwpl.Position.Latitude.Set(prp->m_lat, _T ( "N" ));

            if (prp->m_lon < 0.)
              oNMEA0183.GPwpl.Position.Longitude.Set(-prp->m_lon, _T ( "W" ));
            else
              oNMEA0183.GPwpl.Position.Longitude.Set(prp->m_lon, _T ( "E" ));

            wxString name = prp->GetName();
            name += _T("000000");
            name.Truncate(g_maxWPNameLength);
            oNMEA0183.GPwpl.To = name;

            oNMEA0183.GPwpl.Write(snt);

            g_TalkerIdText = talker_save;
          }

          wxString payload = snt.Sentence;

          // for some gps, like some garmin models, they assume the first
          // waypoint in the route is the boat location, therefore it is
          // dropped. These gps also can only accept a maximum of up to 20
          // waypoints at a time before a delay is needed and a new string of
          // waypoints may be sent. To ensure all waypoints will arrive, we can
          // simply send each one twice. This ensures that the gps  will get the
          // waypoint and also allows us to send as many as we like
          //
          //  We need only send once for FurunoGP3X models

          if (dstr->SendSentence(payload)) {
            if (g_GPS_Ident != _T("FurunoGP3X")) dstr->SendSentence(payload);
            LogOutputMessage(snt.Sentence, dstr->GetPort(), false);
          }

          wxString msg(_T("-->GPS Port:"));
          msg += com_name;
          msg += _T(" Sentence: ");
          msg += snt.Sentence;
          msg.Trim();
          wxLogMessage(msg);

          if (dialog && dialog->GetProgressGauge()) {
            dialog->GetProgressGauge()->SetValue((ip * 100) / nProg);
            dialog->GetProgressGauge()->Refresh();
            dialog->GetProgressGauge()->Update();
          }

          wxMilliSleep(progress_stall);

          node = node->GetNext();

          ip++;
        }
      }

      // Create the NMEA Rte sentence
      // Try to create a single sentence, and then check the length to see if
      // too long
      unsigned int max_length = 76;
      unsigned int max_wp = 2;  // seems to be required for garmin...

      //  Furuno GPS can only accept 5 (five) waypoint linkage sentences....
      //  So, we need to compact a few more points into each link sentence.
      if (g_GPS_Ident == _T("FurunoGP3X")) {
        max_wp = 8;
        max_length = 80;
      }

      //  Furuno has its own talker ID, so do not allow the global override
      wxString talker_save = g_TalkerIdText;
      if (g_GPS_Ident == _T("FurunoGP3X")) g_TalkerIdText.Clear();

      oNMEA0183.Rte.Empty();
      oNMEA0183.Rte.TypeOfRoute = CompleteRoute;

      if (pr->m_RouteNameString.IsEmpty())
        oNMEA0183.Rte.RouteName = _T ( "1" );
      else
        oNMEA0183.Rte.RouteName = pr->m_RouteNameString;

      if (g_GPS_Ident == _T("FurunoGP3X")) {
        oNMEA0183.Rte.RouteName = _T ( "01" );
        oNMEA0183.TalkerID = _T ( "GP" );
        oNMEA0183.Rte.m_complete_char = 'C';  // override the default "c"
        oNMEA0183.Rte.m_skip_checksum = 1;    // no checksum needed
      }

      oNMEA0183.Rte.total_number_of_messages = 1;
      oNMEA0183.Rte.message_number = 1;

      // add the waypoints
      wxRoutePointListNode *node = pr->pRoutePointList->GetFirst();
      while (node) {
        RoutePoint *prp = node->GetData();
        wxString name = prp->GetName().Truncate(g_maxWPNameLength);

        if (g_GPS_Ident == _T("FurunoGP3X")) {
          name = prp->GetName();
          name += _T("000000");
          name.Truncate(g_maxWPNameLength);
          name.Prepend(_T(" "));  // What Furuno calls "Skip Code", space means
                                  // use the WP
        }

        oNMEA0183.Rte.AddWaypoint(name);
        node = node->GetNext();
      }

      oNMEA0183.Rte.Write(snt);

      if ((snt.Sentence.Len() > max_length) ||
          (pr->pRoutePointList->GetCount() >
           max_wp))  // Do we need split sentences?
      {
        // Make a route with zero waypoints to get tare load.
        NMEA0183 tNMEA0183;
        SENTENCE tsnt;
        tNMEA0183.TalkerID = _T ( "EC" );

        tNMEA0183.Rte.Empty();
        tNMEA0183.Rte.TypeOfRoute = CompleteRoute;

        if (g_GPS_Ident != _T("FurunoGP3X")) {
          if (pr->m_RouteNameString.IsEmpty())
            tNMEA0183.Rte.RouteName = _T ( "1" );
          else
            tNMEA0183.Rte.RouteName = pr->m_RouteNameString;

        } else {
          tNMEA0183.Rte.RouteName = _T ( "01" );
        }

        tNMEA0183.Rte.Write(tsnt);

        unsigned int tare_length = tsnt.Sentence.Len();
        tare_length -= 3;  // Drop the checksum, for length calculations

        wxArrayString sentence_array;

        // Trial balloon: add the waypoints, with length checking
        int n_total = 1;
        bool bnew_sentence = true;
        int sent_len = 0;
        unsigned int wp_count = 0;

        wxRoutePointListNode *node = pr->pRoutePointList->GetFirst();
        while (node) {
          RoutePoint *prp = node->GetData();
          unsigned int name_len =
              prp->GetName().Truncate(g_maxWPNameLength).Len();
          if (g_GPS_Ident == _T("FurunoGP3X"))
            name_len = 7;  // six chars, with leading space for "Skip Code"

          if (bnew_sentence) {
            sent_len = tare_length;
            sent_len += name_len + 1;  // with comma
            bnew_sentence = false;
            node = node->GetNext();
            wp_count = 1;

          } else {
            if ((sent_len + name_len > max_length) || (wp_count >= max_wp)) {
              n_total++;
              bnew_sentence = true;
            } else {
              if (wp_count == max_wp)
                sent_len += name_len;  // with comma
              else
                sent_len += name_len + 1;  // with comma
              wp_count++;
              node = node->GetNext();
            }
          }
        }

        // Now we have the sentence count, so make the real sentences using the
        // same counting logic
        int final_total = n_total;
        int n_run = 1;
        bnew_sentence = true;

        node = pr->pRoutePointList->GetFirst();
        while (node) {
          RoutePoint *prp = node->GetData();
          wxString name = prp->GetName().Truncate(g_maxWPNameLength);
          if (g_GPS_Ident == _T("FurunoGP3X")) {
            name = prp->GetName();
            name += _T("000000");
            name.Truncate(g_maxWPNameLength);
            name.Prepend(_T(" "));  // What Furuno calls "Skip Code", space
                                    // means use the WP
          }

          unsigned int name_len = name.Len();

          if (bnew_sentence) {
            sent_len = tare_length;
            sent_len += name_len + 1;  // comma
            bnew_sentence = false;

            oNMEA0183.Rte.Empty();
            oNMEA0183.Rte.TypeOfRoute = CompleteRoute;

            if (g_GPS_Ident != _T("FurunoGP3X")) {
              if (pr->m_RouteNameString.IsEmpty())
                oNMEA0183.Rte.RouteName = _T ( "1" );
              else
                oNMEA0183.Rte.RouteName = pr->m_RouteNameString;
            } else {
              oNMEA0183.Rte.RouteName = _T ( "01" );
            }

            oNMEA0183.Rte.total_number_of_messages = final_total;
            oNMEA0183.Rte.message_number = n_run;
            snt.Sentence.Clear();
            wp_count = 1;

            oNMEA0183.Rte.AddWaypoint(name);
            node = node->GetNext();
          } else {
            if ((sent_len + name_len > max_length) || (wp_count >= max_wp)) {
              n_run++;
              bnew_sentence = true;

              oNMEA0183.Rte.Write(snt);

              sentence_array.Add(snt.Sentence);
            } else {
              sent_len += name_len + 1;  // comma
              oNMEA0183.Rte.AddWaypoint(name);
              wp_count++;
              node = node->GetNext();
            }
          }
        }

        oNMEA0183.Rte.Write(snt);  // last one...
        if (snt.Sentence.Len() > tare_length) sentence_array.Add(snt.Sentence);

        for (unsigned int ii = 0; ii < sentence_array.GetCount(); ii++) {
          wxString sentence = sentence_array[ii];

          if (dstr->SendSentence(sentence))
            LogOutputMessage(sentence, dstr->GetPort(), false);

          wxString msg(_T("-->GPS Port:"));
          msg += com_name;
          msg += _T(" Sentence: ");
          msg += sentence;
          msg.Trim();
          wxLogMessage(msg);

          wxMilliSleep(progress_stall);
        }

      } else {
        if (dstr->SendSentence(snt.Sentence))
          LogOutputMessage(snt.Sentence, dstr->GetPort(), false);

        wxString msg(_T("-->GPS Port:"));
        msg += com_name;
        msg += _T(" Sentence: ");
        msg += snt.Sentence;
        msg.Trim();
        wxLogMessage(msg);
      }

      if (g_GPS_Ident == _T("FurunoGP3X")) {
        wxString name = pr->GetName();
        if (name.IsEmpty()) name = _T("RTECOMMENT");
        wxString rte;
        rte.Printf(_T("$PFEC,GPrtc,01,"));
        rte += name.Left(16);
        wxString rtep;
        rtep.Printf(_T(",%c%c"), 0x0d, 0x0a);
        rte += rtep;
        if (dstr->SendSentence(rte))
          LogOutputMessage(rte, dstr->GetPort(), false);

        wxString msg(_T("-->GPS Port:"));
        msg += com_name;
        msg += _T(" Sentence: ");
        msg += rte;
        msg.Trim();
        wxLogMessage(msg);

        wxString term;
        term.Printf(_T("$PFEC,GPxfr,CTL,E%c%c"), 0x0d, 0x0a);

        if (dstr->SendSentence(term))
          LogOutputMessage(term, dstr->GetPort(), false);

        msg = wxString(_T("-->GPS Port:"));
        msg += com_name;
        msg += _T(" Sentence: ");
        msg += term;
        msg.Trim();
        wxLogMessage(msg);
      }

      if (dialog && dialog->GetProgressGauge()) {
        dialog->GetProgressGauge()->SetValue(100);
        dialog->GetProgressGauge()->Refresh();
        dialog->GetProgressGauge()->Update();
      }

      wxMilliSleep(progress_stall);

      ret_val = 0;

      //  All finished with the temp port
      if (btempStream) dstr->Close();

      if (g_GPS_Ident == _T("FurunoGP3X")) g_TalkerIdText = talker_save;
    }
  }

ret_point_1:

  if (b_restoreStream) {
    if (old_stream) CreateAndRestoreSavedStreamProperties();
  }
#endif
  return ret_val;
}

std::shared_ptr<AbstractCommDriver> CreateOutputConnection(const wxString &com_name,
                                                           std::shared_ptr<AbstractCommDriver> &old_driver,
                                                           ConnectionParams &params_save,
                                                           bool &btempStream, bool &b_restoreStream){

  std::shared_ptr<AbstractCommDriver> driver;
  auto& registry = CommDriverRegistry::getInstance();
  const std::vector<std::shared_ptr<AbstractCommDriver>>& drivers = registry.GetDrivers();

  if (com_name.Lower().StartsWith("serial")) {
    wxString comx = com_name.AfterFirst(':');  // strip "Serial:"
    comx = comx.BeforeFirst(' ');  // strip off any description provided by Windows

    old_driver = FindDriver(drivers, comx.ToStdString());
    wxLogDebug("Looking for old stream %s", com_name);

    if (old_driver) {
      auto drv_serial_n0183 =
         std::dynamic_pointer_cast<CommDriverN0183Serial>(old_driver);
      if (drv_serial_n0183) {
        params_save = drv_serial_n0183->GetParams();
      }
      registry.Deactivate(old_driver);

      b_restoreStream = true;
    }

  } else {
    driver = FindDriver(drivers, com_name.ToStdString());
  }

  if (com_name.Lower().StartsWith("serial")) {
      //  If the port was temporarily closed, reopen as I/O type
      //  Otherwise, open another port using default properties
    int baud;

    if (old_driver) {
      baud = params_save.Baudrate;
    } else {
      baud = 4800;
    }

    ConnectionParams cp;
    cp.Type = SERIAL;
    cp.SetPortStr(com_name);
    cp.Baudrate = baud;
    cp.IOSelect = DS_TYPE_INPUT_OUTPUT;

    driver = MakeCommDriver(&cp);
    btempStream = true;

#ifdef __OCPN__ANDROID__
      wxMilliSleep(1000);
#else
      //  Wait up to 1 seconds for Datastream secondary thread to come up
#if 0
      int timeout = 0;
      while (!dstr->IsSecThreadActive() && (timeout < 50)) {
        wxMilliSleep(100);
        timeout++;
      }

      if (!dstr->IsSecThreadActive()) {
        wxString msg(_T("-->GPS Port:"));
        msg += com_name;
        msg += _T(" ...Could not be opened for writing");
        wxLogMessage(msg);

        dstr->Close();
        goto ret_point;
      }
#endif
#endif
    }
#if 0
    else if (com_name.Find("Bluetooth") != wxNOT_FOUND) {
      if (dstr == NULL) {
        ConnectionParams *pConnectionParams = new ConnectionParams();
        pConnectionParams->Type = INTERNAL_BT;
        wxStringTokenizer tkz(com_name, _T(";"));
        wxString name = tkz.GetNextToken();
        wxString mac = tkz.GetNextToken();

        pConnectionParams->NetworkAddress = name;
        pConnectionParams->Port = mac;
        pConnectionParams->NetworkPort = 0;
        pConnectionParams->NetProtocol = PROTO_UNDEFINED;
        pConnectionParams->Baudrate = 0;

        dstr = makeDataStream(this, pConnectionParams);

        btempStream = true;
      }
    }

    else if (com_name.Lower().StartsWith("udp") ||
             com_name.Lower().StartsWith("tcp")) {
      if (dstr == NULL) {
        NetworkProtocol protocol = UDP;
        if (com_name.Lower().StartsWith("tcp")) protocol = TCP;
        wxStringTokenizer tkz(com_name, _T(":"));
        wxString token = tkz.GetNextToken();
        wxString address = tkz.GetNextToken();
        token = tkz.GetNextToken();
        long port;
        token.ToLong(&port);

        NetworkDataStream *streamNew =
            new NetworkDataStream(this, protocol, address, port);
        if (streamNew->IsOk()) dstr = (DataStream *)streamNew;
        btempStream = true;
      }

      if (com_name.Lower().StartsWith("tcp")) {
        // new tcp connections must wait for connect
        wxString msg = _("Connecting to ");
        msg += com_name;
        dialog->SetMessage(msg);
        dialog->GetProgressGauge()->Pulse();

        NetworkDataStream *streamTest = (NetworkDataStream *)dstr;
        int loopCount = 10;  // seconds
        bool bconnected = false;
        while (!bconnected && (loopCount > 0)) {
          if (streamTest->GetSock()->IsConnected()) {
            bconnected = true;
            break;
          }
          dialog->GetProgressGauge()->Pulse();
          wxYield();
          wxSleep(1);
          loopCount--;
        }
        if (bconnected) {
          msg = _("Connected to ");
          msg += com_name;
          dialog->SetMessage(msg);
        } else {
          if (btempStream) {
            dstr->Close();
            delete dstr;
          }
          return 1;
        }
      }
  }
#endif
  return driver;
}

int SendWaypointToGPS_N0183(RoutePoint *prp, const wxString &com_name/*,SendToGpsDlg *dialog*/) {
  int ret_val = 0;

  ConnectionParams params_save;
  bool b_restoreStream = false;
  bool btempStream = false;
  std::shared_ptr<AbstractCommDriver> old_driver;
  std::shared_ptr<AbstractCommDriver> driver;
  auto& registry = CommDriverRegistry::getInstance();

  driver = CreateOutputConnection(com_name, old_driver,
                                  params_save, btempStream, b_restoreStream);
  auto drv_n0183 = std::dynamic_pointer_cast<CommDriverN0183>(driver);

#ifdef USE_GARMINHOST
  //FIXME (dave)
#ifdef __WXMSW__
  if (com_name.Upper().Matches(_T("*GARMIN*")))  // Garmin USB Mode
  {
    //        if(m_pdevmon)
    //            m_pdevmon->StopIOThread(true);

    int v_init = Garmin_GPS_Init(wxString(_T("usb:")));

    if (v_init < 0) {
      wxString msg(_T(" Garmin USB GPS could not be initialized"));
      wxLogMessage(msg);
      msg.Printf(_T(" Error Code is %d"), v_init);
      wxLogMessage(msg);
      msg = _T(" LastGarminError is: ");
      msg += GetLastGarminError();
      wxLogMessage(msg);

      ret_val = ERR_GARMIN_INITIALIZE;
    } else {
      wxLogMessage(_T("Garmin USB Initialized"));

      wxString msg = _T("USB Unit identifies as: ");
      wxString GPS_Unit = Garmin_GPS_GetSaveString();
      msg += GPS_Unit;
      wxLogMessage(msg);

      wxLogMessage(_T("Sending Waypoint..."));

      // Create a RoutePointList with one item
      RoutePointList rplist;
      rplist.Append(prp);

      int ret1 = Garmin_GPS_SendWaypoints(wxString(_T("usb:")), &rplist);

      if (ret1 != 1) {
        wxLogMessage(_T(" Error Sending Waypoint to Garmin USB"));
        wxString msg;
        msg = _T(" LastGarminError is: ");
        msg += GetLastGarminError();
        wxLogMessage(msg);

        ret_val = ERR_GARMIN_GENERAL;
      } else
        ret_val = 0;
    }

    //        if(m_pdevmon)
    //            m_pdevmon->RestartIOThread();

    return ret_val;
  }
#endif

  // Are we using Garmin Host mode for uploads?
  if (g_bGarminHostUpload) {
    RoutePointList rplist;
    int ret_val;

    wxString short_com = com_name.Mid(7);
    // Initialize the Garmin receiver, build required Jeeps internal data
    // structures
    int v_init = Garmin_GPS_Init(short_com);
    if (v_init < 0) {
      wxString msg(_T("Garmin GPS could not be initialized on port: "));
      msg += com_name;
      wxString err;
      err.Printf(_T(" Error Code is %d"), v_init);
      msg += err;

      msg += _T("\n LastGarminError is: ");
      msg += GetLastGarminError();

      wxLogMessage(msg);

      ret_val = ERR_GARMIN_INITIALIZE;
      goto ret_point;
    } else {
      wxString msg(_T("Sent waypoint(s) to Garmin GPS on port: "));
      msg += com_name;
      msg += _T("\n Unit identifies as: ");
      wxString GPS_Unit = Garmin_GPS_GetSaveString();
      msg += GPS_Unit;
      wxLogMessage(msg);
    }

    // Create a RoutePointList with one item
    rplist.Append(prp);

    ret_val = Garmin_GPS_SendWaypoints(short_com, &rplist);
    if (ret_val != 1) {
      wxString msg(_T("Error Sending Waypoint(s) to Garmin GPS on port: "));
      msg += com_name;
      wxString err;
      err.Printf(_T(" Error Code is %d"), ret_val);
      msg += err;

      msg += _T("\n LastGarminError is: ");
      msg += GetLastGarminError();

      wxLogMessage(msg);

      ret_val = ERR_GARMIN_GENERAL;
      goto ret_point;
    } else
      ret_val = 0;

    goto ret_point;
  } else
#endif  // USE_GARMINHOST

  {  // Standard NMEA mode

    SENTENCE snt;
    NMEA0183 oNMEA0183;
    oNMEA0183.TalkerID = _T ( "EC" );

//FIXME     if (dialog && dialog->GetProgressGauge())
//       dialog->GetProgressGauge()->SetRange(100);

    if (g_GPS_Ident == _T("Generic")) {
      if (prp->m_lat < 0.)
        oNMEA0183.Wpl.Position.Latitude.Set(-prp->m_lat, _T ( "S" ));
      else
        oNMEA0183.Wpl.Position.Latitude.Set(prp->m_lat, _T ( "N" ));

      if (prp->m_lon < 0.)
        oNMEA0183.Wpl.Position.Longitude.Set(-prp->m_lon, _T ( "W" ));
      else
        oNMEA0183.Wpl.Position.Longitude.Set(prp->m_lon, _T ( "E" ));

      oNMEA0183.Wpl.To = prp->GetName().Truncate(g_maxWPNameLength);

      oNMEA0183.Wpl.Write(snt);
    } else if (g_GPS_Ident == _T("FurunoGP3X")) {
      oNMEA0183.TalkerID = _T ( "PFEC," );

      if (prp->m_lat < 0.)
        oNMEA0183.GPwpl.Position.Latitude.Set(-prp->m_lat, _T ( "S" ));
      else
        oNMEA0183.GPwpl.Position.Latitude.Set(prp->m_lat, _T ( "N" ));

      if (prp->m_lon < 0.)
        oNMEA0183.GPwpl.Position.Longitude.Set(-prp->m_lon, _T ( "W" ));
      else
        oNMEA0183.GPwpl.Position.Longitude.Set(prp->m_lon, _T ( "E" ));

      wxString name = prp->GetName();
      name += _T("000000");
      name.Truncate(g_maxWPNameLength);

      oNMEA0183.GPwpl.To = name;

      oNMEA0183.GPwpl.Write(snt);
    }

    auto address = std::make_shared<NavAddr0183>(drv_n0183->iface);
    auto msg_out = std::make_shared<Nmea0183Msg>(std::string("ECWPL"),
                                             snt.Sentence.ToStdString(),
                                             address);

    drv_n0183->SendMessage(msg_out, address);

    //LogOutputMessage(snt.Sentence, com_name, false);

    wxString msg(_T("-->GPS Port:"));
    msg += com_name;
    msg += _T(" Sentence: ");
    msg += snt.Sentence;
    msg.Trim();
    wxLogMessage(msg);

    if (g_GPS_Ident == _T("FurunoGP3X")) {
      wxString term;
      term.Printf(_T("$PFEC,GPxfr,CTL,E%c%c"), 0x0d, 0x0a);

      //driver->SendSentence(term);
      //LogOutputMessage(term, dstr->GetPort(), false);

      wxString msg(_T("-->GPS Port:"));
      msg += com_name;
      msg += _T(" Sentence: ");
      msg += term;
      msg.Trim();
      wxLogMessage(msg);
    }

//     if (dialog && dialog->GetProgressGauge()) {
//       dialog->GetProgressGauge()->SetValue(100);
//       dialog->GetProgressGauge()->Refresh();
//       dialog->GetProgressGauge()->Update();
//     }

    wxMilliSleep(500);

    //  All finished with the temp port
    if (btempStream)
      registry.Deactivate(driver);

    ret_val = 0;
  }

ret_point:
  if (b_restoreStream) {
    if (old_driver){
      MakeCommDriver(&params_save);
    }
  }

  return ret_val;
}
