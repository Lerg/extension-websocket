// Copyright 2020-2022 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// Licensed under the Defold License version 1.0 (the "License"); you may not use
// this file except in compliance with the License.
//
// You may obtain a copy of the License, together with FAQs at
// https://www.defold.com/license
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dmsdk/dlib/array.h>
#include <dmsdk/dlib/log.h>
#include <dmsdk/dlib/dstrings.h>
#include <dmsdk/dlib/time.h>
#include <dmsdk/dlib/math.h>
#include <dlib/socket.h>
#include "http_server.h"

namespace wsHttpServer
{
    const uint32_t BUFFER_SIZE = 64 * 1024;

    struct Connection
    {
        dmSocket::Socket m_Socket;
        uint16_t         m_RequestCount;
        uint64_t         m_ConnectionTimeStart;
    };

    struct Server
    {
        Server()
        {
            m_ServerSocket = dmSocket::INVALID_SOCKET_HANDLE;
            m_Reconnect = 0;
        }
        dmSocket::Address   m_Address;
        uint16_t            m_Port;
        HttpRequest         m_HttpRequest;
        void*               m_Userdata;

        // Connection timeout in useconds. NOTE: In params it is specified in seconds.
        uint64_t            m_ConnectionTimeout;
        dmArray<Connection> m_Connections;
        dmSocket::Socket    m_ServerSocket;
        // Receive and send buffer
        char                m_Buffer[BUFFER_SIZE];

        uint32_t            m_Reconnect : 1;
    };

    void SetDefaultParams(struct NewParams* params)
    {
        memset(params, 0, sizeof(*params));
        params->m_MaxConnections = 16;
        params->m_ConnectionTimeout = 60;
    }

    static void Disconnect(Server* server)
    {
        if (server->m_ServerSocket != dmSocket::INVALID_SOCKET_HANDLE)
        {
            dmSocket::Delete(server->m_ServerSocket);
            server->m_ServerSocket = dmSocket::INVALID_SOCKET_HANDLE;
        }
    }

    static Result Connect(Server* server, uint16_t port)
    {
        dmSocket::Socket socket = dmSocket::INVALID_SOCKET_HANDLE;
        dmSocket::Address bind_address;
        dmSocket::Result r = dmSocket::RESULT_OK;

        Disconnect(server);

        r = dmSocket::GetHostByName("0.0.0.0", &bind_address);
        if (r != dmSocket::RESULT_OK)
        {
            return RESULT_SOCKET_ERROR;
        }

        r = dmSocket::New(bind_address.m_family, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &socket);
        if (r != dmSocket::RESULT_OK)
        {
            return RESULT_UNKNOWN;
        }

        dmSocket::SetReuseAddress(socket, true);

        r = dmSocket::Bind(socket, bind_address, port);
        if (r != dmSocket::RESULT_OK)
        {
            dmSocket::Delete(socket);
            return RESULT_SOCKET_ERROR;
        }

        r = dmSocket::Listen(socket, 32);
        if (r != dmSocket::RESULT_OK)
        {
            dmSocket::Delete(socket);
            return RESULT_SOCKET_ERROR;
        }

        dmSocket::Address address;
        uint16_t actual_port;
        r = dmSocket::GetName(socket, &address, &actual_port);
        if (r != dmSocket::RESULT_OK)
        {
            dmSocket::Delete(socket);
            return RESULT_SOCKET_ERROR;
        }

        server->m_Address = address;
        server->m_Port = actual_port;
        server->m_ServerSocket = socket;

        return RESULT_OK;
    }

    Result New(const NewParams* params, uint16_t port, HServer* server)
    {
        *server = 0;

        if (!params->m_HttpRequest)
            return RESULT_ERROR_INVAL;

        Server* ret = new Server();
        if (Connect(ret, port) != RESULT_OK)
        {
            delete ret;
            return RESULT_SOCKET_ERROR;
        }

        ret->m_HttpRequest = params->m_HttpRequest;
        ret->m_Userdata = params->m_Userdata;
        ret->m_ConnectionTimeout = params->m_ConnectionTimeout * 1000000U;
        ret->m_Connections.SetCapacity(params->m_MaxConnections);

        *server = ret;
        return RESULT_OK;
    }

    void Delete(HServer server)
    {
        // TODO: Shutdown connections
        dmSocket::Delete(server->m_ServerSocket);
        delete server;
    }

    static void HandleRequest(void* user_data, const char* request_method, const char* resource, int major, int minor)
    {
        Request* req = (Request*) user_data;

        dmStrlCpy(req->m_Method, request_method, sizeof(req->m_Method));
        dmStrlCpy(req->m_Resource, resource, sizeof(req->m_Resource));

        if ((major << 16 | minor) < (1 << 16 | 1))
        {
            // Close connection for HTTP protocol version < 1.1
            req->m_CloseConnection = 1;
        } else {

        }
    }

    /*
     * Handle an http-connection
     * Returns false if the connection should be closed
     */
    static bool HandleConnection(Server* server, Connection* connection)
    {
        int total_recv = 0;

        Request internal_req;
        internal_req.m_Result = RESULT_OK;
        internal_req.m_Socket = connection->m_Socket;
        internal_req.m_Server = server;

        server->m_HttpRequest(server->m_Userdata, &internal_req);

        if (internal_req.m_Result == RESULT_OK)
        {
            return (bool) !internal_req.m_CloseConnection;
        }
        else
        {
            return false;
        }
    }

    Result Update(HServer server)
    {
        if (server->m_Reconnect)
        {
            dmLogWarning("Reconnecting http server (%d)", server->m_Port);
            Connect(server, server->m_Port);
            server->m_Reconnect = 0;
        }
        dmSocket::Selector selector;
        dmSocket::SelectorSet(&selector, dmSocket::SELECTOR_KIND_READ, server->m_ServerSocket);

        dmSocket::Result r = dmSocket::Select(&selector, 0);

        if (r != dmSocket::RESULT_OK)
        {
            return RESULT_SOCKET_ERROR;
        }

        // Check for new connections
        if (dmSocket::SelectorIsSet(&selector, dmSocket::SELECTOR_KIND_READ, server->m_ServerSocket))
        {
            dmSocket::Address address;
            dmSocket::Socket client_socket;
            r = dmSocket::Accept(server->m_ServerSocket, &address, &client_socket);
            if (r == dmSocket::RESULT_OK)
            {
                if (server->m_Connections.Full())
                {
                    dmLogWarning("Out of client connections in http server (max: %d)", server->m_Connections.Capacity());
                    dmSocket::Shutdown(client_socket, dmSocket::SHUTDOWNTYPE_READWRITE);
                    dmSocket::Delete(client_socket);
                }
                else
                {
                    dmSocket::SetNoDelay(client_socket, true);
                    Connection connection;
                    memset(&connection, 0, sizeof(connection));
                    connection.m_Socket = client_socket;
                    connection.m_ConnectionTimeStart = dmTime::GetTime();
                    server->m_Connections.Push(connection);
                }
            }
            else if (r == dmSocket::RESULT_CONNABORTED || r == dmSocket::RESULT_NOTCONN)
            {
                server->m_Reconnect = 1;
            }
        }

        dmSocket::SelectorZero(&selector);

        uint64_t current_time = dmTime::GetTime();

        // Iterate over persistent connections, timeout phase
        for (uint32_t i = 0; i < server->m_Connections.Size(); ++i)
        {
            Connection* connection = &server->m_Connections[i];
            uint64_t time_diff = current_time - connection->m_ConnectionTimeStart;
            if (time_diff > server->m_ConnectionTimeout)
            {
                dmSocket::Shutdown(connection->m_Socket, dmSocket::SHUTDOWNTYPE_READWRITE);
                dmSocket::Delete(connection->m_Socket);
                connection->m_Socket = dmSocket::INVALID_SOCKET_HANDLE;
                server->m_Connections.EraseSwap(i);
                --i;
            }
        }

        // Iterate over persistent connections, select phase
        for (uint32_t i = 0; i < server->m_Connections.Size(); ++i)
        {
            Connection* connection = &server->m_Connections[i];
            dmSocket::SelectorSet(&selector, dmSocket::SELECTOR_KIND_READ, connection->m_Socket);
        }

        r = dmSocket::Select(&selector, 0);
        if (r != dmSocket::RESULT_OK)
            return RESULT_SOCKET_ERROR;

        // Iterate over persistent connections, handle phase
        for (uint32_t i = 0; i < server->m_Connections.Size(); ++i)
        {
            Connection* connection = &server->m_Connections[i];
            if (dmSocket::SelectorIsSet(&selector, dmSocket::SELECTOR_KIND_READ, connection->m_Socket))
            {
                bool keep_connection = HandleConnection(server, connection);
                if (!keep_connection)
                {
                    dmSocket::Shutdown(connection->m_Socket, dmSocket::SHUTDOWNTYPE_READWRITE);
                    dmSocket::Delete(connection->m_Socket);
                    connection->m_Socket = dmSocket::INVALID_SOCKET_HANDLE;
                    server->m_Connections.EraseSwap(i);
                    --i;
                }
            }
        }
        return RESULT_OK;
    }

    void GetName(HServer server, dmSocket::Address* address, uint16_t* port)
    {
        *address = server->m_Address;
        *port = server->m_Port;
    }

}
