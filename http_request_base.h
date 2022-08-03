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

#ifndef _HTTP_REQUEST_BASE_H_
#define _HTTP_REQUEST_BASE_H_

#include <map>
#include <string>
#include <vector>
#include "mbed.h"
#include "http_parser.h"
#include "http_parsed_url.h"
#include "http_request_builder.h"
#include "http_request_parser.h"
#include "http_response.h"
#include "NetworkInterface.h"
#include "netsocket/Socket.h"

/**
 * @todo:
 *      - Userinfo parameter is not handled
 */

#ifndef HTTP_RECEIVE_BUFFER_SIZE
#define HTTP_RECEIVE_BUFFER_SIZE 8 * 1024
#endif

class HttpRequest;
class HttpsRequest;

/**
 * \brief HttpRequest implements the logic for interacting with HTTP servers.
 */
class HttpRequestBase {
    friend class HttpRequest;
    friend class HttpsRequest;

  public:
    HttpRequestBase(NetworkInterface *network, Socket *socket, Callback<void(const char *at, uint32_t length)> bodyCallback)
        : _socket(socket), _network(network), _body_callback(bodyCallback), _request_buffer(nullptr), _request_buffer_ix(0) {}

    /**
     * HttpRequest Constructor
     */
    virtual ~HttpRequestBase() {
        // should response be owned by us? Or should user free it?
        // maybe implement copy constructor on response...

        delete _response;

        delete _parsed_url;

        delete _request_builder;

        if (_socket && _we_created_socket) {
            delete _socket;
        }
    }

    /**
     * Execute the request and receive the response.
     * This adds a Content-Length header to the request (when body_size is set), and sends the data to the server.
     * @param body Pointer to the body to be sent
     * @param body_size Size of the body to be sent
     * @return An HttpResponse pointer on success, or NULL on failure.
     *         See get_error() for the error code.
     */
    HttpResponse *send(const void *body = nullptr, nsapi_size_t body_size = 0) {
        nsapi_size_or_error_t ret = connect_socket();

        if (ret != NSAPI_ERROR_OK) {
            _error = ret;
            return nullptr;
        }

        _request_buffer_ix = 0;

        uint32_t request_size = 0;
        char *request = _request_builder->build(body, body_size, request_size);

        ret = send_buffer(request, request_size);

        free(request);

        if (ret < 0) {
            _error = ret;
            return nullptr;
        }

        return create_http_response();
    }

    /**
     * Execute the request and receive the response.
     * This sends the request through chunked-encoding.
     * @param body_cb Callback which generates the next chunk of the request
     * @return An HttpResponse pointer on success, or NULL on failure.
     *         See get_error() for the error code.
     */
    HttpResponse *send(Callback<const void *(uint32_t *)> body_cb) {

        nsapi_error_t ret;

        if ((ret = connect_socket()) != NSAPI_ERROR_OK) {
            _error = ret;
            return nullptr;
        }

        _request_buffer_ix = 0;

        set_header("Transfer-Encoding", "chunked");

        uint32_t request_size = 0;
        char *request = _request_builder->build(nullptr, 0, request_size);

        // first... send this request headers without the body
        nsapi_size_or_error_t total_send_count = send_buffer(request, request_size);

        if (total_send_count < 0) {
            free(request);
            _error = total_send_count;
            return nullptr;
        }

        // ok... now it's time to start sending chunks...
        while (true) {
            uint32_t size;
            const void *buffer = body_cb(&size);

            if (size == 0) break;

            // so... size in HEX, \r\n, data, \r\n again
            char size_buff[10]; // if sending length of more than 8 digits, you have another problem on a microcontroller...
            int size_buff_size = sprintf(size_buff, "%X\r\n", static_cast<size_t>(size));
            if ((total_send_count = send_buffer(size_buff, static_cast<uint32_t>(size_buff_size))) < 0) {
                free(request);
                _error = total_send_count;
                return nullptr;
            }

            // now send the normal buffer... and then \r\n at the end
            total_send_count = send_buffer((char *) buffer, size);
            if (total_send_count < 0) {
                free(request);
                _error = total_send_count;
                return nullptr;
            }

            // and... \r\n
            const char *rn = "\r\n";
            if ((total_send_count = send_buffer((char *) rn, 2)) < 0) {
                free(request);
                _error = total_send_count;
                return nullptr;
            }
        }

        // finalize...?
        const char *fin = "0\r\n\r\n";
        if ((total_send_count = send_buffer((char *) fin, strlen(fin))) < 0) {
            free(request);
            _error = total_send_count;
            return nullptr;
        }

        free(request);

        return create_http_response();
    }

    /**
     * Set a header for the request.
     *
     * The 'Host', 'Content-Length', and (optionally) 'Transfer-Encoding: chunked'
     * headers are set automatically.
     * Setting the same header twice will overwrite the previous entry.
     *
     * @param key Header key
     * @param value Header value
     */
    void set_header(string key, string value) {
        _request_builder->set_header(key, value);
    }

    /**
     * Set content type to json
     */
    void set_json_ct() {
        set_header("Content-Type", "application/json");
    }

    /**
     * Get the error code.
     *
     * When send() fails, this error is set.
     */
    nsapi_error_t get_error() {
        return _error;
    }

    /**
     * Set the request log buffer, all bytes that are sent for this request are logged here.
     * If the buffer would overflow logging is stopped.
     *
     * @param buffer Pointer to a buffer to store the data in
     * @param buffer_size Size of the buffer
     */
    void set_request_log_buffer(uint8_t *buffer, size_t buffer_size) {
        _request_buffer = buffer;
        _request_buffer_size = buffer_size;
        _request_buffer_ix = 0;
    }

    /**
     * Get the number of bytes written to the request log buffer, since the last request.
     * If no request was sent, or if the request log buffer is NULL, then this returns 0.
     */
    size_t get_request_log_buffer_length() {
        return _request_buffer_ix;
    }

  protected:
    virtual nsapi_error_t connect_socket(char *host, uint16_t port) = 0;

  private:
    nsapi_error_t connect_socket() {
        if (_response != NULL) {
            // already executed this response
            return -2100; // @todo, make a lookup table with errors
        }

        if (_we_created_socket) {
            nsapi_error_t connection_result = connect_socket(_parsed_url->host(), _parsed_url->port());
            if (connection_result != NSAPI_ERROR_OK) {
                return connection_result;
            }
        }

        return NSAPI_ERROR_OK;
    }

    nsapi_size_or_error_t send_buffer(char *buffer, nsapi_size_or_error_t buffer_size) {
        nsapi_size_or_error_t total_send_count = 0;
        while (total_send_count < buffer_size) {

            // get a slice of the buffer
            char *buffer_slice = buffer + total_send_count;
            auto buffer_slice_size = buffer_size - total_send_count;

            // if request buffer was set, copy it there
            if (_request_buffer != nullptr && _request_buffer_ix + buffer_slice_size < _request_buffer_size) {
                memcpy(_request_buffer + _request_buffer_ix, buffer_slice, buffer_slice_size);
                _request_buffer_ix += buffer_slice_size;
            }

            nsapi_size_or_error_t send_result = _socket->send(buffer_slice, buffer_slice_size);

            if (send_result < 0) {
                total_send_count = send_result;
                break;
            }

            if (send_result == 0) {
                break;
            }

            total_send_count += send_result;
        }

        return total_send_count;
    }

    HttpResponse *create_http_response() {
        // Create a response object
        _response = new HttpResponse();
        // And a response parser
        HttpParser parser(_response, HTTP_RESPONSE, _body_callback);

        // Set up a receive buffer (on the heap)
        auto *recv_buffer = (uint8_t *) malloc(HTTP_RECEIVE_BUFFER_SIZE);

        // Socket::recv is called until we don't have any data anymore
        nsapi_size_or_error_t recv_ret;
        while ((recv_ret = _socket->recv(recv_buffer, HTTP_RECEIVE_BUFFER_SIZE)) > 0) {

            // Pass the chunk into the http_parser
            auto nparsed = static_cast<nsapi_size_or_error_t>(parser.execute((const char *) recv_buffer, recv_ret));
            if (nparsed != recv_ret) {
                // printf("Parsing failed... parsed %d bytes, received %d bytes\n", nparsed, recv_ret);
                _error = -2101;
                free(recv_buffer);
                return nullptr;
            }

            if (_response->is_message_complete()) {
                break;
            }
        }
        // error?
        if (recv_ret < 0) {
            _error = recv_ret;
            free(recv_buffer);
            return nullptr;
        }

        // When done, call parser.finish()
        parser.finish();

        // Free the receive buffer
        free(recv_buffer);

        if (_we_created_socket) {
            // Close the socket
            _socket->close();
        }

        return _response;
    }

  private:
    Socket *_socket;
    NetworkInterface *_network;
    Callback<void(const char *at, uint32_t length)> _body_callback;

    ParsedUrl *_parsed_url{};

    HttpRequestBuilder *_request_builder{};
    HttpResponse *_response{};

    bool _we_created_socket{};

    nsapi_error_t _error{};

    uint8_t *_request_buffer;
    size_t _request_buffer_size{};
    size_t _request_buffer_ix;
};

#endif // _HTTP_REQUEST_BASE_H_
