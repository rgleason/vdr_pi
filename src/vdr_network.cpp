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

// Socket event IDs
enum { SOCKET_ID = 5000, SERVER_ID };

BEGIN_EVENT_TABLE(VDRNetworkServer, wxEvtHandler)
EVT_SOCKET(SERVER_ID, VDRNetworkServer::OnTcpEvent)
EVT_SOCKET(SOCKET_ID, VDRNetworkServer::OnTcpEvent)
END_EVENT_TABLE()

VDRNetworkServer::VDRNetworkServer()
    : m_tcpServer(nullptr),
      m_udpSocket(nullptr),
      m_running(false),
      m_useTCP(true),
      m_port(kDefaultPort) {
  // Initialize socket handling
  wxSocketBase::Initialize();
}

VDRNetworkServer::~VDRNetworkServer() {
  if (m_running) {
    Stop();
  }
}

bool VDRNetworkServer::Start(bool useTCP, int port, wxString& error) {
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

void VDRNetworkServer::Stop() {
  if (m_tcpServer) {
    m_tcpServer->Notify(false);
    delete m_tcpServer;
    m_tcpServer = nullptr;
  }

  if (m_udpSocket) {
    delete m_udpSocket;
    m_udpSocket = nullptr;
  }

  m_tcpClients.clear();
  m_running = false;
}

bool VDRNetworkServer::SendText(const wxString& message) {
  if (!m_running) {
    return false;
  }

  // Ensure message ends with proper line ending
  wxString formattedMsg = message;
  if (!formattedMsg.EndsWith("\r\n")) {
    formattedMsg += "\r\n";
  }
  return SendImpl(formattedMsg.c_str(), formattedMsg.Length());
}

bool VDRNetworkServer::SendBinary(const void* data, size_t length) {
  if (!m_running || !data || length == 0) {
    return false;
  }

  return SendImpl(data, length);
}

bool VDRNetworkServer::SendImpl(const void* data, size_t length) {
  if (m_useTCP) {
    // Remove any dead connections before sending
    CleanupDeadConnections();

    // Send to all TCP clients
    bool success = true;
    for (auto client : m_tcpClients) {
      client->Write(data, length);
      if (client->Error()) {
        success = false;
      }
    }
    return success && !m_tcpClients.empty();
  } else {
    // Send UDP broadcast to localhost
    if (m_udpSocket) {
      wxIPV4address destAddr;
      destAddr.Hostname("127.0.0.1");
      destAddr.Service(m_port);  // Target port (10110 typically)

      m_udpSocket->SendTo(destAddr, data, length);
      return !m_udpSocket->Error();
    }
  }
  return false;
}

bool VDRNetworkServer::InitTCP(int port, wxString& error) {
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
  if (m_tcpServer) {
    delete m_tcpServer;
    m_tcpServer = nullptr;
  }

  m_tcpServer = new wxSocketServer(addr);

  // Check socket state
  if (!m_tcpServer->IsOk()) {
    error = _("TCP server init failed");
    wxLogMessage(error);
    delete m_tcpServer;
    m_tcpServer = nullptr;
    return false;
  }

  m_tcpServer->SetEventHandler(*this);
  // Indicate that we want to be notified on connection events.
  m_tcpServer->SetNotify(wxSOCKET_CONNECTION_FLAG);
  // Enable the event notifications.
  m_tcpServer->Notify(true);
  error = "";
  wxLogMessage("TCP server initialized on port %d", port);
  return true;
}

bool VDRNetworkServer::InitUDP(int port, wxString& error) {
  // Create new socket
  if (m_udpSocket) {
    delete m_udpSocket;
    m_udpSocket = nullptr;
  }

  wxIPV4address addr;
  addr.AnyAddress();
  addr.Service(0);  // Use ephemeral port for sending

  m_udpSocket = new wxDatagramSocket(addr, wxSOCKET_NOWAIT);
  // Check socket state
  if (!m_udpSocket->IsOk()) {
    error = _("UDP socket init failed");
    wxLogMessage(error);
    delete m_udpSocket;
    m_udpSocket = nullptr;
    return false;
  }
  error = "";
  wxLogMessage("UDP server initialized on port %d", port);
  return true;
}

void VDRNetworkServer::OnTcpEvent(wxSocketEvent& event) {
  switch (event.GetSocketEvent()) {
    case wxSOCKET_CONNECTION: {
      // Accept new client connection
      wxSocketBase* client = m_tcpServer->Accept(false);
      if (client) {
        client->SetEventHandler(*this, SOCKET_ID);
        client->SetNotify(wxSOCKET_LOST_FLAG);
        client->Notify(true);
        m_tcpClients.push_back(client);
        wxLogMessage("New TCP client connected. Total clients: %zu",
                     m_tcpClients.size());
      }
      break;
    }

    case wxSOCKET_LOST: {
      // Handle client disconnection
      wxSocketBase* client = event.GetSocket();
      if (client) {
        auto it = std::find(m_tcpClients.begin(), m_tcpClients.end(), client);
        if (it != m_tcpClients.end()) {
          m_tcpClients.erase(it);
          client->Destroy();
          wxLogMessage("TCP client disconnected. Remaining clients: %zu",
                       m_tcpClients.size());
        }
      }
      break;
    }

    default:
      break;
  }
}

void VDRNetworkServer::CleanupDeadConnections() {
  auto it = m_tcpClients.begin();
  while (it != m_tcpClients.end()) {
    wxSocketBase* client = *it;
    if (!client || !client->IsConnected()) {
      delete client;
      it = m_tcpClients.erase(it);
    } else {
      ++it;
    }
  }
}
