/***************************************************************************
 *   Copyright (C) 2011  Jean-Eudes Onfray                                 *
 *   Copyright (C) 2025  Sebastian Rosser                                  *
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

#ifndef VDR_NETWORK_H_
#define VDR_NETWORK_H_

#include <wx/wx.h>
#include <wx/socket.h>

#include <vector>
#include <memory>

// Forward declarations
class wxSocketServer;
class wxDatagramSocket;
class wxSocketEvent;

/**
 * Network server for replaying NMEA messages over TCP or UDP.
 *
 * Provides a server that can listen on a specified port and protocol (TCP/UDP)
 * and broadcast messages to connected clients. For TCP, maintains a list of
 * connected clients. For UDP, broadcasts to localhost on the specified port.
 */
class VDRNetworkServer : public wxEvtHandler {
public:
  /** Constructor initializes server state. */
  VDRNetworkServer();

  /** Destructor ensures proper cleanup of sockets. */
  ~VDRNetworkServer() override;

  /**
   * Start the network server.
   *
   * @param useTCP True to use TCP, false for UDP.
   * @param port Port number to listen on.
   * * @param error Will contain error message if start fails
   * @return True if server started successfully
   */
  bool Start(bool useTCP, int port, wxString& error);

  /** Stop the server and cleanup all connections. */
  void Stop();

  /**
   * Send a text message to all connected clients
   *
   * For text-based formats like SeaSmart ($PCDIN), Actisense ASCII, etc.
   * Automatically adds line endings if needed.
   *
   * @param message Text message to send
   * @return True if message was sent successfully
   */
  bool SendText(const wxString& message);

  /**
   * Send binary data to all connected clients
   *
   * For binary formats like Actisense N2K, RAW, NGT.
   * Sends data exactly as provided without any modification.
   *
   * @param data Pointer to binary data
   * @param length Length of data in bytes
   * @return True if data was sent successfully
   */
  bool SendBinary(const void* data, size_t length);

  /** Check if server is currently running. */
  [[nodiscard]] bool IsRunning() const { return m_running; }

  /** Get current protocol (TCP/UDP). */
  [[nodiscard]] bool IsTCP() const { return m_useTCP; }

  /** Get current port number. */
  [[nodiscard]] int GetPort() const { return m_port; }

private:
  /** Handle incoming TCP socket events. */
  void OnTcpEvent(wxSocketEvent& event);

  /** Remove any dead or disconnected TCP clients. */
  void CleanupDeadConnections();

  /** Initialize TCP server. */
  bool InitTCP(int port, wxString& error);

  /** Initialize UDP server. */
  bool InitUDP(int port, wxString& error);

  /**
   * Internal send implementation.
   * Handles the actual sending of data for both TCP and UDP.
   */
  bool SendImpl(const void* data, size_t length);

private:
  wxSocketServer* m_tcpServer;              //!< TCP server socket
  wxDatagramSocket* m_udpSocket;            //!< UDP socket
  std::vector<wxSocketBase*> m_tcpClients;  //!< Connected TCP clients
  bool m_running;                           //!< Server running state
  bool m_useTCP;                            //!< Current protocol
  int m_port;                               //!< Current port

  static constexpr int kDefaultPort = 10111;  //!< Default NMEA port

  DECLARE_EVENT_TABLE()
};

#endif  // VDR_NETWORK_H_
