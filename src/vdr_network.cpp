/***************************************************************************
 *   Copyright (C) 2011  Jean-Eudes Onfray                                 *
 *   Copyright (C) 2025  Sebastian Rosset                                  *
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
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

#include "vdr_network.h"

#include <algorithm>

// Avoid strange wxDFEFINE_EVENT(...) macro:
static const wxEventTypeTag<wxSocketEvent> EvtTcpSocket(wxNewEventType());

VdrNetworkServer::VdrNetworkServer()
    : m_tcp_server(nullptr),
      m_udp_socket(nullptr),
      m_running(false),
      m_useTCP(true),
      m_port(kDefaultPort) {
  // Initialize socket handling
  wxSocketBase::Initialize();
  Bind(EvtTcpSocket, [&](wxSocketEvent& ev) { OnTcpEvent(ev); });
}

VdrNetworkServer::~VdrNetworkServer() {
  if (m_running) {
    Stop();
  }
}

bool VdrNetworkServer::Start(bool useTCP, int port, wxString& error) {
  // Don't start if already running
  if (m_running) {
    Stop();  // Stop first to reconfigure
  }

  m_useTCP = useTCP;
  m_port = port;

  // Validate port number
  if (port < 1024 || port > 65535) {
    error = wxString::Format("Invalid port %d (must be 1024-65535)", port);
    wxLogMessage(error);
    return false;
  }

  bool success = m_useTCP ? InitTCP(port, error) : InitUDP(port, error);

  if (success) {
    m_running = true;
    error = "";
    wxLogMessage("VDR Network Server started - %s on port %d",
                 m_useTCP ? "TCP" : "UDP", m_port);
  }
  return success;
}

void VdrNetworkServer::Stop() {
  if (m_tcp_server) {
    m_tcp_server->Notify(false);
    delete m_tcp_server;
    m_tcp_server = nullptr;
  }

  if (m_udp_socket) {
    delete m_udp_socket;
    m_udp_socket = nullptr;
  }

  m_tcp_clients.clear();
  m_running = false;
}

bool VdrNetworkServer::SendText(const wxString& message) {
  if (!m_running) {
    return false;
  }

  // Ensure message ends with proper line ending
  wxString formatted_msg = message;
  if (!formatted_msg.EndsWith("\r\n")) {
    formatted_msg += "\r\n";
  }
  return SendImpl(formatted_msg.c_str(), formatted_msg.Length());
}

bool VdrNetworkServer::SendBinary(const void* data, size_t length) {
  if (!m_running || !data || length == 0) {
    return false;
  }

  return SendImpl(data, length);
}

bool VdrNetworkServer::SendImpl(const void* data, size_t length) {
  if (m_useTCP) {
    // Remove any dead connections before sending
    CleanupDeadConnections();

    // Send to all TCP clients
    bool success = true;
    for (auto client : m_tcp_clients) {
      client->Write(data, length);
      if (client->Error()) {
        success = false;
      }
    }
    return success && !m_tcp_clients.empty();
  } else {
    // Send UDP broadcast to localhost
    if (m_udp_socket) {
      wxIPV4address dest_addr;
      dest_addr.Hostname("127.0.0.1");
      dest_addr.Service(m_port);  // Target port (10110 typically)

      m_udp_socket->SendTo(dest_addr, data, length);
      return !m_udp_socket->Error();
    }
  }
  return false;
}

bool VdrNetworkServer::InitTCP(int port, wxString& error) {
  wxIPV4address addr;
  if (!addr.Hostname("127.0.0.1")) {
    error = "Failed to set TCP socket hostname";
    wxLogMessage(error);
    return false;
  }

  if (!addr.Service(port)) {
    error = wxString::Format("Failed to set TCP port %d", port);
    wxLogMessage(error);
    return false;
  }

  // Create new server socket
  if (m_tcp_server) {
    delete m_tcp_server;
    m_tcp_server = nullptr;
  }

  m_tcp_server = new wxSocketServer(addr);

  // Check socket state
  if (!m_tcp_server->IsOk()) {
    error = _("TCP server init failed");
    wxLogMessage(error);
    delete m_tcp_server;
    m_tcp_server = nullptr;
    return false;
  }

  m_tcp_server->SetEventHandler(*this);
  // Indicate that we want to be notified on connection events.
  m_tcp_server->SetNotify(wxSOCKET_CONNECTION_FLAG);
  // Enable the event notifications.
  m_tcp_server->Notify(true);
  error = "";
  wxLogMessage("TCP server initialized on port %d", port);
  return true;
}

bool VdrNetworkServer::InitUDP(int port, wxString& error) {
  // Create new socket
  if (m_udp_socket) {
    delete m_udp_socket;
    m_udp_socket = nullptr;
  }

  wxIPV4address addr;
  addr.AnyAddress();
  addr.Service(0);  // Use ephemeral port for sending

  m_udp_socket = new wxDatagramSocket(addr, wxSOCKET_NOWAIT);
  // Check socket state
  if (!m_udp_socket->IsOk()) {
    error = _("UDP socket init failed");
    wxLogMessage(error);
    delete m_udp_socket;
    m_udp_socket = nullptr;
    return false;
  }
  error = "";
  wxLogMessage("UDP server initialized on port %d", port);
  return true;
}

void VdrNetworkServer::OnTcpEvent(wxSocketEvent& event) {
  switch (event.GetSocketEvent()) {
    case wxSOCKET_CONNECTION: {
      // Accept new client connection
      wxSocketBase* client = m_tcp_server->Accept(false);
      if (client) {
        client->SetEventHandler(*this, EvtTcpSocket);
        client->SetNotify(wxSOCKET_LOST_FLAG);
        client->Notify(true);
        m_tcp_clients.push_back(client);
        wxLogMessage("New TCP client connected. Total clients: %zu",
                     m_tcp_clients.size());
      }
      break;
    }

    case wxSOCKET_LOST: {
      // Handle client disconnection
      wxSocketBase* client = event.GetSocket();
      if (client) {
        auto it = std::find(m_tcp_clients.begin(), m_tcp_clients.end(), client);
        if (it != m_tcp_clients.end()) {
          m_tcp_clients.erase(it);
          client->Destroy();
          wxLogMessage("TCP client disconnected. Remaining clients: %zu",
                       m_tcp_clients.size());
        }
      }
      break;
    }

    default:
      break;
  }
}

void VdrNetworkServer::CleanupDeadConnections() {
  auto it = m_tcp_clients.begin();
  while (it != m_tcp_clients.end()) {
    wxSocketBase* client = *it;
    if (!client || !client->IsConnected()) {
      delete client;
      it = m_tcp_clients.erase(it);
    } else {
      ++it;
    }
  }
}
