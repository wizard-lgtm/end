#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum HttpMethods {
    GET,
    POST,
    DELETE,
    PUT,
    UNKNOWN
};

typedef struct Request {
    enum HttpMethods method;
    char* path;
    char* version;
    char* headers;
    char* body;
} Request;

void print_request(const Request* request) {
    if (!request) {
        printf("Invalid request.\n");
        return;
    }

    const char* method;
    switch (request->method) {
        case GET: method = "GET"; break;
        case POST: method = "POST"; break;
        case PUT: method = "PUT"; break;
        case DELETE: method = "DELETE"; break;
        default: method = "UNKNOWN"; break;
    }

    printf("Method: %s\n", method);
    printf("Path: %s\n", request->path);
    printf("Version: %s\n", request->version);
    printf("Headers: %s\n", request->headers);
    printf("Body: %s\n", request->body);
}

void free_request(Request* request) {
    if (!request) return;

    // Free the allocated memory for each member
    free(request->path);
    free(request->version);
    free(request->headers);
    free(request->body);

    // Free the Request structure itself
    free(request);
}

enum HttpMethods parse_method(char* method) {
    if (strcmp(method, "GET") == 0) return GET;
    if (strcmp(method, "POST") == 0) return POST;
    if (strcmp(method, "PUT") == 0) return PUT;
    if (strcmp(method, "DELETE") == 0) return DELETE;
    return UNKNOWN;
}

int main() {
    // Allocate request
    Request* req = (Request*)malloc(sizeof(Request));
    if (!req) return 1;

    const char *http_request = 
    "POST /path/resource HTTP/1.1\r\n"
    "Host: www.example.com\r\n"
    "User-Agent: MyCustomClient/1.0\r\n"
    "Accept: */*\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 47\r\n"
    "Connection: close\r\n"
    "\r\n"
    "{ \"name\": \"John Doe\", \"email\": \"john@example.com\" }";

    // Allocate buffer for the HTTP request copy
    char* buffer = malloc(sizeof(char) * 1024);
    strcpy(buffer, http_request); 
    
    // Find the position of "\r\n\r\n"
    char* separator = strstr(buffer, "\r\n\r\n");

    if (separator != NULL) {
        // Null-terminate the header part
        *separator = '\0';  
        char* head = buffer; // The header starts at the beginning
        char* body = separator + 4; // The body starts after the \r\n\r\n delimiter

        // Now, split header into status line and headers
        // Find the first "\r\n" to separate status line from headers
        char* header_end = strstr(head, "\r\n");
        if (header_end != NULL) {
            *header_end = '\0';  // Null-terminate the status line
            char* status_line = head;  // The status line is at the beginning
            char* headers = header_end + 2;  // The headers start after the first \r\n

            // Parse the status line
            char* method_end = strtok(status_line, " ");
            req->method = parse_method(method_end); // Parse method
            req->path = strdup(strtok(NULL, " "));  // Get path
            req->version = strdup(strtok(NULL, " ")); // Get version

            // Copy headers and body into the request structure
            req->headers = strdup(headers);
            req->body = strdup(body);

            // Print the request
            print_request(req);
        } else {
            printf("Error: Could not find status line delimiter.\n");
        }
    } else {
        printf("Error: Could not find header-body separator.\n");
    }

    // Free memory
    free_request(req);
    free(buffer);
    
    return 0;
}
