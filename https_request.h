/*
 * PackageLicenseDeclared: Apache-2.0
 * Copyright (c) 2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _MBED_HTTPS_REQUEST_H_
#define _MBED_HTTPS_REQUEST_H_

#include <string>
#include <vector>
#include <map>
#include "http_request_base.h"
#include "TLSSocket.h"

#ifndef HTTP_RECEIVE_BUFFER_SIZE
#define HTTP_RECEIVE_BUFFER_SIZE 8 * 1024
#endif

/**
 * \brief HttpsRequest implements the logic for interacting with HTTPS servers.
 */
class HttpsRequest : public HttpRequestBase {
  public:

    /**
 * HttpsRequest Constructor
 * Initializes the TCP socket, sets up event handlers and flags.
 *
 * @param[in] network The network interface
 * @param[in] ssl_ca_pem String containing the trusted CAs
 * @param[in] method HTTP method to use
 * @param[in] url URL to the resource
 * @param[in] body_callback Callback on which to retrieve chunks of the response body.
                            If not set, the complete body will be allocated on the HttpResponse object,
                            which might use lots of memory.
 */
    HttpsRequest(NetworkInterface *network,
                 http_method method,
                 const char *url,
                 Callback<void(const char *at, uint32_t length)> body_callback = 0)
        : HttpRequestBase(network, nullptr, body_callback) {
        _parsed_url = new ParsedUrl(url);
        _request_builder = new HttpRequestBuilder(method, _parsed_url);
        _response = nullptr;

        _socket = new TLSSocket();
        ((TLSSocket *) _socket)->open(network);
        _we_created_socket = true;
        this->disable_ssl_verify();
    }

    /**
     * HttpsRequest Constructor
     * Initializes the TCP socket, sets up event handlers and flags.
     *
     * @param[in] network The network interface
     * @param[in] ssl_ca_pem String containing the trusted CAs
     * @param[in] method HTTP method to use
     * @param[in] url URL to the resource
     * @param[in] body_callback Callback on which to retrieve chunks of the response body.
                                If not set, the complete body will be allocated on the HttpResponse object,
                                which might use lots of memory.
     */
    HttpsRequest(NetworkInterface *network,
                 const char *ssl_ca_pem,
                 http_method method,
                 const char *url,
                 Callback<void(const char *at, uint32_t length)> body_callback = 0)
        : HttpRequestBase(network, nullptr, body_callback) {
        _parsed_url = new ParsedUrl(url);
        _request_builder = new HttpRequestBuilder(method, _parsed_url);
        _response = nullptr;

        _socket = new TLSSocket();
        ((TLSSocket *) _socket)->open(network);
        ((TLSSocket *) _socket)->set_root_ca_cert(ssl_ca_pem);
        _we_created_socket = true;
    }

    /**
     * HttpsRequest Constructor
     * Sets up event handlers and flags.
     *
     * @param[in] socket A connected TLSSocket
     * @param[in] method HTTP method to use
     * @param[in] url URL to the resource
     * @param[in] body_callback Callback on which to retrieve chunks of the response body.
                                If not set, the complete body will be allocated on the HttpResponse object,
                                which might use lots of memory.
     */
    HttpsRequest(NetworkInterface *network,
                 TLSSocket *socket,
                 http_method method,
                 const char *url,
                 Callback<void(const char *at, uint32_t length)> body_callback = 0)
        : HttpRequestBase(network, socket, body_callback) {
        _parsed_url = new ParsedUrl(url);
        _body_callback = body_callback;
        _request_builder = new HttpRequestBuilder(method, _parsed_url);
        _response = NULL;

        _we_created_socket = false;
    }

    virtual ~HttpsRequest() {}

    void disable_ssl_verify() {
        mbedtls_ssl_config *conf = ((TLSSocket *) this->_socket)->get_ssl_config();
        mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
    }

  protected:
    virtual nsapi_error_t connect_socket(char *host, uint16_t port) {
        SocketAddress addr;

        int result = _network->gethostbyname(host, &addr);
        if (result != NSAPI_ERROR_OK) {
            return result;
        }

        addr.set_port(port);

        return ((TCPSocket *) _socket)->connect(addr);
    }
};

#endif // _MBED_HTTPS_REQUEST_H_
