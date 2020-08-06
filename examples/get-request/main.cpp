#include "mbedtls_entropy_config.h"
#include "mbed.h"
#include "http_request.h"
#include "network-helper.h"
#include "mbed_mem_trace.h"

void dump_response(HttpResponse* res) {
    printf("Status: %d - %s\n", res->get_status_code(), res->get_status_message().c_str());

    printf("Headers:\n");
    for (size_t ix = 0; ix < res->get_headers_length(); ix++) {
        printf("\t%s: %s\n", res->get_headers_fields()[ix]->c_str(), res->get_headers_values()[ix]->c_str());
    }
    printf("\nBody (%d bytes):\n\n%s\n", res->get_body_length(), res->get_body_as_string().c_str());
}

int main() {
    // Connect to the network with the default networking interface
    // if you use WiFi: see mbed_app.json for the credentials
    NetworkInterface* network = connect_to_default_network_interface();
    if (!network) {
        printf("Cannot connect to the network, see serial output\n");
        return 1;
    }

    // By default the body is automatically parsed and stored in a buffer, this is memory heavy.
    // To receive chunked response, pass in a callback as last parameter to the constructor.
    HttpRequest* get_req = new HttpRequest(network, HTTP_GET, "http://httpbin.org/status/418");

    HttpResponse* get_res = get_req->send();
    if (!get_res) {
        printf("HttpRequest failed (error code %d)\n", get_req->get_error());
        return 1;
    }

    printf("\n----- HTTP GET response -----\n");
    dump_response(get_res);

    delete get_req;

    return 0;
}
