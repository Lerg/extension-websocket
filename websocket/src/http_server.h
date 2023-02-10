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

#ifndef WS_HTTP_SERVER_H
#define WS_HTTP_SERVER_H

#include <dlib/socket.h>

namespace wsHttpServer
{
    /**
     * @file
     * Simple single-threaded HTTP server with multiple persistent clients supported.
     * Http methods sending data, eg put and post, are not supported.
     */

    /**
     * Http-server handle
     */
    typedef struct Server* HServer;

    /**
     * Result codes
     */
    enum Result
    {
        RESULT_OK = 0,               //!< RESULT_OK
        RESULT_SOCKET_ERROR = -1,    //!< RESULT_SOCKET_ERROR
        RESULT_INVALID_REQUEST = -2, //!< RESULT_INVALID_REQUEST
        RESULT_ERROR_INVAL = -3,     //!< RESULT_ERROR_INVAL
        RESULT_INTERNAL_ERROR = -100,//!< RESULT_INTERNAL_ERROR
        RESULT_UNKNOWN = -1000,      //!< RESULT_UNKNOWN
    };

    /**
     * Set NewParams default values
     * @param params Pointer to NewParams
     */
    void SetDefaultParams(struct NewParams* params);

    /**
     * Http request. Contains relevant information about the request. Passed into #HttpResponse callback
     */
    struct Request
    {
        Result   m_Result;
        dmSocket::Socket m_Socket;
        Server*  m_Server;

        char     m_Method[16];
        char     m_Resource[128];

        int      m_StatusCode;

        // Offset to where content start in buffer. This the extra content read while parsing headers
        // The value is adjusted in Receive when data is consumed
        uint32_t m_ContentOffset;
        // Total amount of data received in Server.m_Buffer
        uint32_t m_TotalReceived;

        // Total content received, ie the payload
        uint32_t m_TotalContentReceived;

        // Number of bytes in send buffer
        uint32_t m_SendBufferPos;

        uint16_t m_CloseConnection : 1;
        uint16_t m_HeaderSent : 1;
        uint16_t m_AttributesSent : 1;

        Request()
        {
            memset(this, 0, sizeof(*this));
            m_StatusCode = 200;
        }
    };

    /**
     * Http request callback. Called when request is received.
     * @param user_data User data
     * @param request Request information
     */
    typedef void (*HttpRequest)(void* user_data, Request* request);

    /**
     * Parameters passed into #New when creating a new server instance
     */
    struct NewParams
    {
        /// User-data. Passed in to callbacks
        void*       m_Userdata;

        /// HTTP-request callback
        HttpRequest m_HttpRequest;

        /// Max persistent client connections
        uint16_t    m_MaxConnections;

        /// Connection timeout in seconds
        uint16_t    m_ConnectionTimeout;

        NewParams()
        {
            SetDefaultParams(this);
        }
    };

    /**
     * Create a new http server instance
     * @param params Parameters
     * @param port Port to run the server on
     * @param server Http server instance
     * @return RESULT_OK on success
     */
    Result New(const NewParams* params, uint16_t port, HServer* server);

    /**
     * Delete http server instance
     * @param server Http server instance handle
     */
    void Delete(HServer server);

    /**
     * Update http server, eg serve and accept connections
     * @param server Http server instance
     * @return RESULT_OK on success
     */
    Result Update(HServer server);

    /**
     * Get name for socket, ie address and port
     * @param server Http server instance
     * @param address Address (result)
     * @param port Port (result)
     */
    void GetName(HServer server, dmSocket::Address* address, uint16_t* port);
}

#endif
