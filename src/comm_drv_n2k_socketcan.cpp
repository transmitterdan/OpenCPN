/***************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Implement comm_drv_socketcan.h -- socketcan driver.
 * Author:   David Register, Alec Leamas
 *
 ***************************************************************************
 *   Copyright (C) 2022 by David Register, Alec Leamas                     *
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

#if !defined(__linux__) || defined(__ANDROID__)
#error "This file can only be compiled on Linux"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <serial/serial.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <wx/log.h>
#include <wx/string.h>
#include <wx/utils.h>

#include "comm_drv_n2k_socketcan.h"
#include "comm_drv_registry.h"
#include "comm_navmsg_bus.h"

using namespace std::chrono_literals;

using TimePoint = std::chrono::time_point<std::chrono::system_clock,
                                          std::chrono::duration<double>>;

static const int kNotFound = -1;

/// Number of fast messsages stored triggering Garbage Collection.
static const int kGcThreshold = 100;

/// Max time between garbage collection runs.
static const std::chrono::milliseconds kGcInterval(10s);

/// Max entry age before garbage collected
static const std::chrono::milliseconds kEntryMaxAge(100s);

/// Read timeout in worker main loop (seconds)
static const int kSocketTimeoutSeconds = 2;

typedef struct can_frame CanFrame;


unsigned long BuildCanID(int priority, int source, int destination, int pgn) {
  // build CanID
  unsigned long cid = 0;
  unsigned char pf = (unsigned char) (pgn >> 8);
  if (pf < 240){
    cid = ((unsigned long)(priority & 0x7))<<26 | pgn<<8 | ((unsigned long)destination)<<8 | (unsigned long)source;
  }
  else {
    cid = ((unsigned long)(priority & 0x7))<<26 | pgn<<8 | (unsigned long)source;
  }
  return cid;
}

/// CAN v2.0 29 bit header as used by NMEA 2000
class CanHeader {
public:
  CanHeader() : priority('\0'), source('\0'), destination('\0'), pgn(-1) {};

  /** Construct a CanHeader by parsing a frame */
  CanHeader(CanFrame frame);

  /** Return true if header reflects a multipart fast message. */
  bool IsFastMessage() const;

  unsigned char priority;
  unsigned char source;
  unsigned char destination;
  int pgn;
};

/** Track fast message fragments eventually forming complete messages. */
class FastMessageMap {
public:
  class Entry {
  public:
    Entry()
        : time_arrived(std::chrono::system_clock::now()),
          sid(0), expected_length(0), cursor(0) {}

    bool IsExpired() const {
      auto age = std::chrono::system_clock::now() - time_arrived;
      return age > kEntryMaxAge;
    }

    TimePoint time_arrived;  ///< time of last fragment.

    /// Can header, used to "map" the incoming fast message fragments
    CanHeader header;

    /// Sequence identifier, used to check if a received message is the
    /// next message in the sequence
    unsigned int sid;

    unsigned int expected_length;  ///< total data length from first frame
    unsigned int cursor;  ///< cursor into the current position in data.
    std::vector<unsigned char> data;  ///< Received data
  };

  FastMessageMap() : dropped_frames(0) {}

  Entry operator[](int i) const { return entries[i]; }  /// Getter
  Entry& operator[](int i) { return entries[i]; }       /// Setter

  /** Return index to entry matching header and sid or -1 if not found. */
  int FindMatchingEntry(const CanHeader header, const unsigned char sid);

  /** Allocate a new, fresh entry and return index to it. */
  int AddNewEntry(void);

  /** Insert a new entry, first part of a multipart message. */
  bool InsertEntry(const CanHeader header, const unsigned char* data,
                   int index);

  /** Append fragment to existing multipart message. */
  bool AppendEntry(const CanHeader hdr, const unsigned char* data, int index);

  /** Remove entry at pos. */
  void Remove(int pos);

private:
  int GarbageCollector(void);

  void CheckGc() {
    if (std::chrono::system_clock::now() - last_gc_run > kGcInterval ||
        entries.size() > kGcThreshold) {
      GarbageCollector();
      last_gc_run = std::chrono::system_clock::now();
    }
  }

  std::vector<Entry> entries;
  TimePoint last_gc_run;
  int dropped_frames;
  TimePoint dropped_frame_time;
};

class CommDriverN2KSocketCanImpl;  // fwd

/**
 * Manages reading the N2K data stream provided by some N2K gateways from the
 * declared serial port.
 *
 * Commonly used raw format is actually inherited from an old paketizing format:
 * <10><02><application data><CRC (1)><10><03>
 *
 * Actisense application data, from NGT-1 to PC
 * <data code=93><length (1)><priority (1)><PGN (3)><destination(1)><source
 * (1)><time (4)><len (1)><data (len)>
 *
 * As applied to a real application data element, after extraction from packet
 * format: 93 13 02 01 F8 01 FF 01 76 C2 52 00 08 08 70 EB 14 E8 8E 52 D2 BB 10
 *
 * length (1):      0x13
 * priority (1);    0x02
 * PGN (3):         0x01 0xF8 0x01
 * destination(1):  0xFF
 * source (1):      0x01
 * time (4):        0x76 0xC2 0x52 0x00
 * len (1):         0x08
 * data (len):      08 70 EB 14 E8 8E 52 D2
 * packet CRC:      0xBB
 */
class Worker {
public:
  Worker(CommDriverN2KSocketCAN* parent, const wxString& PortName);

  bool StartThread();
  void StopThread();
  int GetSocket(){return m_socket;}

private:
  void Entry();

  void ThreadMessage(const std::string& msg, wxLogLevel l = wxLOG_Message);

  int InitSocket(const std::string port_name);
  void SocketMessage(const std::string& msg, const std::string& device);
  void HandleInput(CanFrame frame);

  std::vector<unsigned char> PushCompleteMsg(const CanHeader header,
                                             int position,
                                             const CanFrame frame);
  std::vector<unsigned char> PushFastMsgFragment(const CanHeader& header,
                                                 int position);

  CommDriverN2KSocketCanImpl* const m_parent_driver;
  const wxString m_port_name;
  std::atomic<int> m_run_flag;
  FastMessageMap fast_messages;
  int m_socket;
};

/** Local driver implementation, not visible outside this file.*/
class CommDriverN2KSocketCanImpl : public CommDriverN2KSocketCAN {
  friend class Worker;

public:
  CommDriverN2KSocketCanImpl(const ConnectionParams* p, DriverListener& l)
      : CommDriverN2KSocketCAN(p, l), m_worker(this, p->socketCAN_port) {
    Open();
  }

  ~CommDriverN2KSocketCanImpl() { Close(); }

  bool Open();
  void Close();

  bool SendMessage(std::shared_ptr<const NavMsg> msg,
                    std::shared_ptr<const NavAddr> addr);


  Worker& GetWorker(){ return m_worker; }

private:
  Worker m_worker;
  unsigned int m_source_address;
};


// Static CommDriverN2KSocketCAN factory implementation.

std::shared_ptr<CommDriverN2KSocketCAN> CommDriverN2KSocketCAN::Create(
    const ConnectionParams* params, DriverListener& listener) {
  return std::shared_ptr<CommDriverN2KSocketCAN>(
      new CommDriverN2KSocketCanImpl(params, listener));
}


// CanHeader implementation

CanHeader::CanHeader(const CanFrame frame) {
  unsigned char buf[4];
  buf[0] = frame.can_id & 0xFF;
  buf[1] = (frame.can_id >> 8) & 0xFF;
  buf[2] = (frame.can_id >> 16) & 0xFF;
  buf[3] = (frame.can_id >> 24) & 0xFF;

  source = buf[0];
  destination = buf[2] < 240 ? buf[1] : 255;
  pgn = (buf[3] & 0x01) << 16 | (buf[2] << 8) | (buf[2] < 240 ? 0 : buf[1]);
  priority = (buf[3] & 0x1c) >> 2;

  if (pgn == 59904){
    unsigned char *d = (unsigned char *)&frame;
    for (size_t i=0 ; i < sizeof(frame) ; i++){
      printf("%02X ", *d);
      d++;
    }
    printf("\n\n");
  }
}


bool CanHeader::IsFastMessage() const {
  static const std::vector<unsigned> haystack = {
      // All known multiframe fast messages
      65240u,  126208u, 126464u, 126996u, 126998u, 127233u, 127237u, 127489u,
      127496u, 127506u, 128275u, 129029u, 129038u, 129039u, 129040u, 129041u,
      129284u, 129285u, 129540u, 129793u, 129794u, 129795u, 129797u, 129798u,
      129801u, 129802u, 129808u, 129809u, 129810u, 130065u, 130074u, 130323u,
      130577u, 130820u, 130822u, 130824u};

  unsigned needle = static_cast<unsigned>(pgn);
  auto found = std::find_if(haystack.begin(), haystack.end(),
                            [needle](unsigned i) { return i == needle; });
  return found != haystack.end();
}


// CommDriverN2KSocketCanImpl implementation

bool CommDriverN2KSocketCanImpl::Open() {
  m_source_address = 77;  //FIXME

  return m_worker.StartThread();
}

void CommDriverN2KSocketCanImpl::Close() {
  wxLogMessage("Closing N2K socketCAN: %s", m_params.socketCAN_port.c_str());
  m_worker.StopThread();

  // We cannot use shared_from_this() since we might be in the destructor.
  auto& registry = CommDriverRegistry::GetInstance();
  auto me = FindDriver(registry.GetDrivers(), iface, bus);
  registry.Deactivate(me);
}

bool CommDriverN2KSocketCanImpl::SendMessage(std::shared_ptr<const NavMsg> msg,
                                        std::shared_ptr<const NavAddr> addr) {

  CanFrame frame;
  memset(&frame, 0, sizeof(frame));

  auto msg_n2k = std::dynamic_pointer_cast<const Nmea2000Msg>(msg);
  std::vector<uint8_t> load = msg_n2k->payload;

  uint64_t _pgn = msg_n2k->name.value;

  unsigned long canId = BuildCanID(6, m_source_address, 255, _pgn);

  frame.can_id = canId | CAN_EFF_FLAG;
  frame.can_dlc = load.size();

  if (frame.can_dlc > 8)    // skip multiframe
    return false;

  if (load.size() > 0)
    memcpy(&frame.data, load.data(), load.size());

  int socket = GetWorker().GetSocket();

  if (socket < 0)
    return false;

  CanHeader tt(frame);

  unsigned char *d = (unsigned char *)&frame;
  printf("SendFrame: ");
  for (size_t i=0 ; i < sizeof(frame) ; i++){
    printf("%02X ", *d);
    d++;
  }
  printf("\n\n");

  int sentbytes = write(socket, &frame, sizeof(frame));


  return false;
}



// CommDriverN2KSocketCAN implementation

CommDriverN2KSocketCAN::CommDriverN2KSocketCAN(const ConnectionParams* params,
                                               DriverListener& listener)
    : CommDriverN2K(((ConnectionParams*)params)->GetStrippedDSPort()),
      m_params(*params),
      m_listener(listener),
      m_ok(false),
      m_portstring(params->GetDSPort()),
      m_baudrate(wxString::Format("%i", params->Baudrate)) {}

CommDriverN2KSocketCAN::~CommDriverN2KSocketCAN() {}

void CommDriverN2KSocketCAN::Activate() {
  CommDriverRegistry::GetInstance().Activate(shared_from_this());
  // TODO(dave) Finally decide if thread should start here or in CTOR
}


// Worker implementation

Worker::Worker(CommDriverN2KSocketCAN* parent, const wxString& port_name)
    : m_parent_driver(dynamic_cast<CommDriverN2KSocketCanImpl*>(parent)),
      m_port_name(port_name.Clone()),
      m_run_flag(-1),
      m_socket(-1) {
  assert(m_parent_driver != 0);
}

std::vector<unsigned char> Worker::PushCompleteMsg(const CanHeader header,
                                                   int position,
                                                   const CanFrame frame) {
  std::vector<unsigned char> data;
  data.push_back(0x93);
  data.push_back(0x13);
  data.push_back(header.priority);
  data.push_back(header.pgn & 0xFF);
  data.push_back((header.pgn >> 8) & 0xFF);
  data.push_back((header.pgn >> 16) & 0xFF);
  data.push_back(header.destination);
  data.push_back(header.source);
  data.push_back(0xFF);  // FIXME (dave) generate the time fields
  data.push_back(0xFF);
  data.push_back(0xFF);
  data.push_back(0xFF);
  data.push_back(CAN_MAX_DLEN);  // nominally 8
  for (size_t n = 0; n < CAN_MAX_DLEN; n++) data.push_back(frame.data[n]);
  data.push_back(0x55);  // CRC dummy, not checked
  return data;
}

std::vector<unsigned char> Worker::PushFastMsgFragment(const CanHeader& header,
                                                       int position) {
  std::vector<unsigned char> data;
  data.push_back(0x93);
  data.push_back(fast_messages[position].expected_length + 11);
  data.push_back(header.priority);
  data.push_back(header.pgn & 0xFF);
  data.push_back((header.pgn >> 8) & 0xFF);
  data.push_back((header.pgn >> 16) & 0xFF);
  data.push_back(header.destination);
  data.push_back(header.source);
  data.push_back(0xFF);  // FIXME (dave) Could generate the time fields
  data.push_back(0xFF);
  data.push_back(0xFF);
  data.push_back(0xFF);
  data.push_back(fast_messages[position].expected_length);
  for (size_t n = 0; n < fast_messages[position].expected_length; n++)
    data.push_back(fast_messages[position].data[n]);
  data.push_back(0x55);  // CRC dummy
  fast_messages.Remove(position);
  return data;
}

void Worker::ThreadMessage(const std::string& msg, wxLogLevel level) {
  wxLogGeneric(level, wxString(msg.c_str()));
  auto s = std::string("CommDriverN2KSocketCAN: ") + msg;
  CommDriverRegistry::GetInstance().evt_driver_msg.Notify(level, s);
}

void Worker::SocketMessage(const std::string& msg, const std::string& device) {
  std::stringstream ss;
  ss << msg << device << ": " << strerror(errno);
  ThreadMessage(ss.str());
}

/**
 * Initiate can socket
 * @param port_name Name of device, for example "can0" (native) , "slcan0"
 *                  (serial) or "vcan0" (virtual)
 * @return positive socket number or -1 on errors.
 */
int Worker::InitSocket(const std::string port_name) {
  int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (sock < 0) {
    SocketMessage("SocketCAN socket create failed: ", port_name);
    return -1;
  }

  // Get the interface index
  struct ifreq if_request;
  strcpy(if_request.ifr_name, port_name.c_str());
  if (ioctl(sock, SIOCGIFINDEX, &if_request) < 0) {
    SocketMessage("SocketCAN ioctl (SIOCGIFINDEX) failed: ", port_name);
    return -1;
  }

  // Check if interface is UP
  struct sockaddr_can can_address;
  can_address.can_family = AF_CAN;
  can_address.can_ifindex = if_request.ifr_ifindex;
  if (ioctl(sock, SIOCGIFFLAGS, &if_request) < 0) {
    SocketMessage("SocketCAN socket IOCTL (SIOCGIFFLAGS) failed: ", port_name);
    return -1;
  }
  if (if_request.ifr_flags & IFF_UP) {
    ThreadMessage("socketCan interface is UP");
  } else {
    ThreadMessage("socketCan interface is NOT UP");
    return -1;
  }

  // Set timeout and bind
  struct timeval tv;
  tv.tv_sec = kSocketTimeoutSeconds;
  tv.tv_usec = 0;
  int r =
      setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
  if (r < 0) {
    SocketMessage("SocketCAN setsockopt SO_RCVTIMEO failed on device: ",
                  port_name);
    return -1;
  }
  r = bind(sock, (struct sockaddr*)&can_address, sizeof(can_address));
  if (r < 0) {
    SocketMessage("SocketCAN socket bind() failed: ", port_name);
    return -1;
  }
  return sock;
}

/**
 * Handle a frame. A complete message or last part of a multipart fast
 * message is sent to m_listener, basically making it available to upper
 * layers. Otherwise, the fast message fragment is stored waiting for
 * next fragment.
 */
void Worker::HandleInput(CanFrame frame) {
  int position = -1;
  bool ready = true;

  CanHeader header(frame);
  if (header.IsFastMessage()) {
    position = fast_messages.FindMatchingEntry(header, frame.data[0]);
    if (position == kNotFound) {
      // Not an existing fast message: create new entry and insert first frame
      position = fast_messages.AddNewEntry();
      ready = fast_messages.InsertEntry(header, frame.data, position);
    } else {
      // An existing fast message entry is present, append the frame
      ready = fast_messages.AppendEntry(header, frame.data, position);
    }
  }
  if (ready) {
    std::vector<unsigned char> vec;
    if (position >= 0) {
      // Re-assembled fast message
      vec = PushFastMsgFragment(header, position);
    } else {
      // Single frame message
      vec = PushCompleteMsg(header, position, frame);
    }
    auto name = N2kName(static_cast<uint64_t>(header.pgn));
    auto src_addr = m_parent_driver->GetAddress(name);
    auto msg = std::make_shared<const Nmea2000Msg>(name, vec, src_addr);
    m_parent_driver->m_listener.Notify(std::move(msg));
  }
}

/** Worker thread main function. */
void Worker::Entry() {
  int recvbytes;
  int socket;
  CanFrame frame;

  socket = InitSocket(m_port_name.ToStdString());
  if (socket < 0) {
    std::string msg("SocketCAN socket create failed: ");
    ThreadMessage(msg + m_port_name.ToStdString());
    return;
  }
  m_socket = socket;

  // The main loop
  while (m_run_flag > 0) {
    recvbytes = read(socket, &frame, sizeof(frame));
    if (recvbytes == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) continue;  // timeout

      wxLogWarning("can socket %s: fatal error %s", m_port_name.c_str(),
                   strerror(errno));
      break;
    }
    if (recvbytes != 16) {
      wxLogWarning("can socket %s: bad frame size: %d (ignored)",
                   m_port_name.c_str(), recvbytes);
      sleep(1);
      continue;
    }
    HandleInput(frame);
  }
  m_run_flag = -1;
  return;
}

bool Worker::StartThread() {
  m_run_flag = 1;
  std::thread t(&Worker::Entry, this);
  t.detach();
  return true;
}

void Worker::StopThread() {
  if (m_run_flag < 0) {
    wxLogMessage("Attempt to stop already dead thread (ignored).");
    return;
  }
  wxLogMessage("Stopping Worker Thread");

  m_run_flag = 0;
  int tsec = 10;
  while ((m_run_flag >= 0) && (tsec--)) wxSleep(1);

  if (m_run_flag < 0)
    wxLogMessage("StopThread: Stopped in %d sec.", 10 - tsec);
  else
    wxLogWarning("StopThread: Not Stopped after 10 sec.");
}

//  FastMessage implementation

int FastMessageMap::FindMatchingEntry(const CanHeader header,
                                      const unsigned char sid) {
  for (unsigned i = 0; i < entries.size(); i++) {
    if (((sid & 0xE0) == (entries[i].sid & 0xE0)) &&
        (entries[i].header.pgn == header.pgn) &&
        (entries[i].header.source == header.source) &&
        (entries[i].header.destination == header.destination)) {
      return i;
    }
  }
  return kNotFound;
}

int FastMessageMap::AddNewEntry(void) {
  entries.push_back(Entry());
  return entries.size() - 1;
}

int FastMessageMap::GarbageCollector(void) {
  std::vector<unsigned> stale_entries;
  for (unsigned i = 0; i < entries.size(); i++) {
    if (entries[i].IsExpired()) stale_entries.push_back(i);
  }
  for (auto i : stale_entries) Remove(i);
  return stale_entries.size();
}

bool FastMessageMap::InsertEntry(const CanHeader header,
                                 const unsigned char* data, int index) {
  // first message of fast packet
  // data[0] Sequence Identifier (sid)
  // data[1] Length of data bytes
  // data[2..7] 6 data bytes

  CheckGc();
  // Ensure that this is indeed the first frame of a fast message
  if ((data[0] & 0x1F) == 0) {
    int total_data_len;  // will also include padding as we memcpy all of the
                         // frame, because I'm lazy
    total_data_len = static_cast<unsigned int>(data[1]);
    total_data_len += 7 - ((total_data_len - 6) % 7);

    entries[index].sid = static_cast<unsigned int>(data[0]);
    entries[index].expected_length = static_cast<unsigned int>(data[1]);
    entries[index].header = header;
    entries[index].time_arrived = std::chrono::system_clock::now();

    entries[index].data.resize(total_data_len);
    memcpy(&entries[index].data[0], &data[2], 6);
    // First frame of a multi-frame Fast Message contains six data bytes.
    // Position the cursor ready for next message
    entries[index].cursor = 6;

    // Fusion, using fast messages to sends frames less than eight bytes
    return entries[index].expected_length <= 6;
  }
  return false;
  // No further processing is performed if this is not a start frame.
  // A start frame may have been dropped and we received a subsequent frame
}

bool FastMessageMap::AppendEntry(const CanHeader header,
                                 const unsigned char* data, int position) {
  // Check that this is the next message in the sequence
  if ((entries[position].sid + 1) == data[0]) {
    memcpy(&entries[position].data[entries[position].cursor], &data[1], 7);
    entries[position].sid = data[0];
    // Subsequent messages contains seven data bytes (last message may be padded
    // with 0xFF)
    entries[position].cursor += 7;
    // Is this the last message ?
    return entries[position].cursor >= entries[position].expected_length;
  } else if ((data[0] & 0x1F) == 0) {
    // We've found a matching entry, however this is a start frame, therefore
    // we've missed an end frame, and now we have a start frame with the same id
    // (top 3 bits). The id has obviously rolled over. Should really double
    // check that (data[0] & 0xE0) Clear the entry as we don't want to leak
    // memory, prior to inserting a start frame
    entries.erase(entries.begin() + position);
    position = AddNewEntry();
    // And now insert it
    InsertEntry(header, data, position);
    // FIXME (dave) Should update the dropped frame stats
    return true;
  } else {
    // This is not the next frame in the sequence and not a start frame
    // We've dropped an intermedite frame, so free the slot and do no further
    // processing
    entries.erase(entries.begin() + position);
    // Dropped Frame Statistics
    if (dropped_frames == 0) {
      dropped_frame_time = std::chrono::system_clock::now();
      dropped_frames += 1;
    } else {
      dropped_frames += 1;
    }
    // FIXME (dave)
    //     if ((dropped_frames > CONST_DROPPEDFRAME_THRESHOLD) &&
    //     (wxDateTime::Now() < (dropped_frame_time +
    //     wxTimeSpan::Seconds(CONST_DROPPEDFRAME_PERIOD) ) ) ) {
    //       wxLogError(_T("TwoCan Device, Dropped Frames rate exceeded"));
    //       wxLogError(wxString::Format(_T("Frame: Source: %d Destination: %d
    //       Priority: %d PGN: %d"),header.source, header.destination,
    //       header.priority, header.pgn)); dropped_frames = 0;
    //     }
    return false;
  }
}

void FastMessageMap::Remove(int pos) { entries.erase(entries.begin() + pos); }
