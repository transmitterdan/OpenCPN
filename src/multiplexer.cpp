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
#include <wx/msw/winundef.h>
#endif

#include "config.h"

#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif

#if defined(HAVE_READLINK) && !defined(HAVE_LIBGEN_H)
#error Using readlink(3) requires libgen.h which cannot be found.
#endif

#include <wx/wx.h>

#include "multiplexer.h"

#include "config_vars.h"
#include "conn_params.h"
#include "comm_drv_registry.h"
#include "comm_drv_n0183_serial.h"
#include "comm_drv_n0183_net.h"
#include "comm_navmsg_bus.h"

#ifdef __linux__
#include "udev_rule_mgr.h"
#endif

extern bool g_b_legacy_input_filter_behaviour;

wxDEFINE_EVENT(EVT_N0183_MUX, ObservedEvt);

wxDEFINE_EVENT(EVT_N2K_129029, ObservedEvt);
wxDEFINE_EVENT(EVT_N2K_129025, ObservedEvt);
wxDEFINE_EVENT(EVT_N2K_129026, ObservedEvt);
wxDEFINE_EVENT(EVT_N2K_127250, ObservedEvt);
wxDEFINE_EVENT(EVT_N2K_129540, ObservedEvt);

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

Multiplexer::Multiplexer(MuxLogCallbacks cb) : m_log_callbacks(cb) {
  auto &msgbus = NavMsgBus::GetInstance();

  m_listener_N0183_all.Listen(Nmea0183Msg::MessageKey("ALL"), this,
                              EVT_N0183_MUX);
  Bind(EVT_N0183_MUX, [&](ObservedEvt ev) {
    auto ptr = ev.GetSharedPtr();
    auto n0183_msg = std::static_pointer_cast<const Nmea0183Msg>(ptr);
    HandleN0183(n0183_msg);
  });

  InitN2KCommListeners();
  n_N2K_repeat = 0;

  if (g_GPS_Ident.IsEmpty()) g_GPS_Ident = wxT("Generic");
}

Multiplexer::~Multiplexer() {}

void Multiplexer::LogOutputMessageColor(const wxString &msg,
                                        const wxString &stream_name,
                                        const wxString &color) {
  if (m_log_callbacks.log_is_active()) {
    wxDateTime now = wxDateTime::Now();
    wxString ss;
#ifndef __WXQT__  //  Date/Time on Qt are broken, at least for android
    ss = now.FormatISOTime();
#endif
    ss.Prepend(_T("--> "));
    ss.Append(_T(" ("));
    ss.Append(stream_name);
    ss.Append(_T(") "));
    ss.Append(msg);
    ss.Prepend(color);

    m_log_callbacks.log_message(ss.ToStdString());
  }
}

void Multiplexer::LogOutputMessage(const wxString &msg, wxString stream_name,
                                   bool b_filter) {
  if (b_filter)
    LogOutputMessageColor(msg, stream_name, _T("<CORAL>"));
  else
    LogOutputMessageColor(msg, stream_name, _T("<BLUE>"));
}

void Multiplexer::LogInputMessage(const wxString &msg,
                                  const wxString &stream_name, bool b_filter,
                                  bool b_error) {
  if (m_log_callbacks.log_is_active()) {
    wxDateTime now = wxDateTime::Now();
    wxString ss;
#ifndef __WXQT__  //  Date/Time on Qt are broken, at least for android
    ss = now.FormatISOTime();
#endif
    ss.Append(_T(" ("));
    ss.Append(stream_name);
    ss.Append(_T(") "));
    ss.Append(msg);
    if (b_error) {
      ss.Prepend(_T("<RED>"));
    } else {
      if (b_filter)
        if (g_b_legacy_input_filter_behaviour)
          ss.Prepend(_T("<CORAL>"));
        else
          ss.Prepend(_T("<MAROON>"));
      else
        ss.Prepend(_T("<GREEN>"));
    }
    m_log_callbacks.log_message(ss.ToStdString());
  }
}

void Multiplexer::HandleN0183(std::shared_ptr<const Nmea0183Msg> n0183_msg) {
  // Find the driver that originated this message

  const auto& drivers = CommDriverRegistry::GetInstance().GetDrivers();
  auto source_driver = FindDriver(drivers, n0183_msg->source->iface);

  wxString fmsg;
  bool bpass_input_filter = true;

  // Send to the Debug Window, if open
  //  Special formatting for non-printable characters helps debugging NMEA
  //  problems
  if (m_log_callbacks.log_is_active()) {
    std::string str = n0183_msg->payload;

    // Get the params for the driver sending this message
      ConnectionParams params;
      auto drv_serial =
          std::dynamic_pointer_cast<CommDriverN0183Serial>(source_driver);
      if (drv_serial) {
        params = drv_serial->GetParams();
      } else {
        auto drv_net = std::dynamic_pointer_cast<CommDriverN0183Net>(source_driver);
        if (drv_net) {
          params = drv_net->GetParams();
        }
      }

    // Check to see if the message passes the source's input filter
    bpass_input_filter = params.SentencePassesFilter(n0183_msg->payload.c_str(),
                                        FILTER_INPUT);

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

    // FIXME (dave)  Flag checksum errors, but fix and process the sentence anyway
    //std::string goodMessage(message);
    //bool checksumOK = CheckSumCheck(event.GetNMEAString());
    //if (!checksumOK) {
    //goodMessage = stream->FixChecksum(goodMessage);
    //goodEvent->SetNMEAString(goodMessage);
    //}


    wxString port(n0183_msg->source->iface);
    LogInputMessage(fmsg, port, !bpass_input_filter, b_error);
  }

  // Detect virtual driver, message comes from plugin API
  // Set such source iface to "" for later test
  std::string source_iface;
  if (source_driver)        // NULL for virtual driver
    source_iface = source_driver->iface;


  // Perform multiplexer output functions
  for (auto& driver : drivers) {

    if (driver->bus == NavAddr::Bus::N0183) {
      ConnectionParams params;
      auto drv_serial =
          std::dynamic_pointer_cast<CommDriverN0183Serial>(driver);
      if (drv_serial) {
        params = drv_serial->GetParams();
      } else {
        auto drv_net = std::dynamic_pointer_cast<CommDriverN0183Net>(driver);
        if (drv_net) {
          params = drv_net->GetParams();
        }
      }

      if ((g_b_legacy_input_filter_behaviour && !bpass_input_filter) ||
           bpass_input_filter) {

      //  Allow re-transmit on same port (if type is SERIAL),
      //  or any any other NMEA0183 port supporting output
      //  But, do not echo to the source network interface.  This will likely recurse...
        if (params.Type == SERIAL || driver->iface != source_iface) {
          if (params.IOSelect == DS_TYPE_INPUT_OUTPUT ||
              params.IOSelect == DS_TYPE_OUTPUT)
          {
            bool bout_filter = true;
            bool bxmit_ok = true;
            if (params.SentencePassesFilter(n0183_msg->payload.c_str(),
                                          FILTER_OUTPUT)) {
              bxmit_ok = driver->SendMessage(n0183_msg,
                                std::make_shared<NavAddr0183>(driver->iface));
              bout_filter = false;
            }

            // Send to the Debug Window, if open
            if (!bout_filter) {
              if (bxmit_ok)
                LogOutputMessageColor(fmsg, driver->iface, _T("<BLUE>"));
              else
                LogOutputMessageColor(fmsg, driver->iface, _T("<RED>"));
            } else
              LogOutputMessageColor(fmsg, driver->iface, _T("<CORAL>"));
          }
        }
      }
    }
  }
}

void Multiplexer::InitN2KCommListeners() {
  // Initialize the comm listeners
  auto& msgbus = NavMsgBus::GetInstance();

  // Create a series of N2K listeners
  // to allow minimal N2K Debug window logging

  // GNSS Position Data PGN  129029
  //----------------------------------
  Nmea2000Msg n2k_msg_129029(static_cast<uint64_t>(129029));
  listener_N2K_129029.Listen(n2k_msg_129029, this, EVT_N2K_129029);
  Bind(EVT_N2K_129029, [&](ObservedEvt ev) {
    HandleN2K_Log(UnpackEvtPointer<Nmea2000Msg>(ev));
  });

  // Position rapid   PGN 129025
  //-----------------------------
  Nmea2000Msg n2k_msg_129025(static_cast<uint64_t>(129025));
  listener_N2K_129025.Listen(n2k_msg_129025, this, EVT_N2K_129025);
  Bind(EVT_N2K_129025, [&](ObservedEvt ev) {
    HandleN2K_Log(UnpackEvtPointer<Nmea2000Msg>(ev));
  });

  // COG SOG rapid   PGN 129026
  //-----------------------------
  Nmea2000Msg n2k_msg_129026(static_cast<uint64_t>(129026));
  listener_N2K_129026.Listen(n2k_msg_129026, this, EVT_N2K_129026);
  Bind(EVT_N2K_129026, [&](ObservedEvt ev) {
    HandleN2K_Log(UnpackEvtPointer<Nmea2000Msg>(ev));
  });

  // Heading rapid   PGN 127250
  //-----------------------------
  Nmea2000Msg n2k_msg_127250(static_cast<uint64_t>(127250));
  listener_N2K_127250.Listen(n2k_msg_127250, this, EVT_N2K_127250);
  Bind(EVT_N2K_127250, [&](ObservedEvt ev) {
    HandleN2K_Log(UnpackEvtPointer<Nmea2000Msg>(ev));
  });

  // GNSS Satellites in View   PGN 129540
  //-----------------------------
  Nmea2000Msg n2k_msg_129540(static_cast<uint64_t>(129540));
  listener_N2K_129540.Listen(n2k_msg_129540, this, EVT_N2K_129540);
  Bind(EVT_N2K_129540, [&](ObservedEvt ev) {
    HandleN2K_Log(UnpackEvtPointer<Nmea2000Msg>(ev));
  });
}

bool Multiplexer::HandleN2K_Log(std::shared_ptr<const Nmea2000Msg> n2k_msg) {

  if (n2k_msg->PGN.pgn == last_pgn_logged) {
    n_N2K_repeat++;
    return false;
  }
  else {
    if(n_N2K_repeat) {
      wxString repeat_log_msg;
      repeat_log_msg.Printf("...Repeated %d times\n", n_N2K_repeat);
      LogInputMessage(repeat_log_msg, "N2000", false, false);
      n_N2K_repeat = 0;
    }
  }

  wxString log_msg;
  log_msg.Printf("%s : %s\n", n2k_msg->PGN.to_string().c_str(), N2K_LogMessage_Detail(n2k_msg).c_str());

  LogInputMessage(log_msg, "N2000", false, false);

  last_pgn_logged = n2k_msg->PGN.pgn;
  return true;
}


std::string Multiplexer::N2K_LogMessage_Detail(std::shared_ptr<const Nmea2000Msg> n2k_msg) {

  switch (n2k_msg->PGN.pgn){
    case 129029:
      return "GNSS Position";
      break;
    case 129025:
      return "Position rapid";
      break;
    case 129026:
      return "COG/SOG rapid";
      break;
    case 127250:
      return "Heading rapid";
      break;
    case 129540:
      return "GNSS Sats";
      break;
    default:
      return "";
  }
}
