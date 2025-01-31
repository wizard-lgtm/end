#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>

#define PORT 8001
#define INITIAL_BUFFER_SIZE 1024
#define COUNTER_PATH "counter.txt"

typedef struct Header {
    char* key;
    char* value;
    struct Header* next;
} Header;
enum HttpMethods{
	GET,
	POST,
	DELETE,
	PUT,
	UNKNOWN
};
enum HttpMimeTypes {
    MIME_TEXT_HTML,
    MIME_TEXT_PLAIN,
    MIME_TEXT_CSS,
    MIME_APPLICATION_JSON,
    MIME_APPLICATION_JAVASCRIPT,
    MIME_IMAGE_PNG,
    MIME_IMAGE_JPEG,
    MIME_IMAGE_GIF,
    MIME_VIDEO_MP4,
    MIME_APPLICATION_OCTET_STREAM,
    MIME_UNKNOWN
};

const char* mime_type_to_string(enum HttpMimeTypes mimeType) {
    switch (mimeType) {
        case MIME_TEXT_HTML: return "text/html";
        case MIME_TEXT_PLAIN: return "text/plain";
        case MIME_TEXT_CSS: return "text/css";
        case MIME_APPLICATION_JSON: return "application/json";
        case MIME_APPLICATION_JAVASCRIPT: return "application/javascript";
        case MIME_IMAGE_PNG: return "image/png";
        case MIME_IMAGE_JPEG: return "image/jpeg";
        case MIME_IMAGE_GIF: return "image/gif";
        case MIME_VIDEO_MP4: return "video/mp4";
        case MIME_APPLICATION_OCTET_STREAM: return "application/octet-stream";
        default: return "unknown";
    }
}

typedef struct Request {
    enum HttpMethods method;
    char* path;
    char* version;
    Header* headers;  // Changed to linked list of headers
    char* body;
} Request;
typedef struct {
	char* version;
	char* status_code;
	char* status_message;
    enum HttpMimeTypes content_type;
	Header* headers;
	char* body;
} Response;



char* http_read_buffer(int fd); // Done
Request* http_parse_request(char* buffer); // Done.
void http_route_request(Request* req, int client_fd);
char* http_render_response(Response* response);
void http_write_buffer(char* buffer);
void print_response(Response*);
void free_response(Response*);
void free_request(Request*);


int socket_fd;
struct sockaddr_in addr;


void http_route_request(Request* req, int client_fd) {
    char* path = req->path;

    if (strcmp(path, "/") == 0) {
        Response* res = (Response*)malloc(sizeof(Response));
        if (res == NULL) {
            perror("Failed to allocate memory for response");
            exit(EXIT_FAILURE);
        }

        res->version = strdup("HTTP/1.1");
        res->status_code = strdup("200");
        res->status_message = strdup("OK");

        char* body = "kick my ass";
        res->body = strdup(body);
        if (res->body == NULL) {
            perror("Failed to allocate memory for response body");
            exit(EXIT_FAILURE);
        }

        res->headers = NULL; // No headers in this simple example

        char* rendered_response = http_render_response(res);
        printf("repsonse str is:\n%s\n", rendered_response);

        // Send repsonse_str to client
        int bytes_written = write(client_fd, rendered_response, strlen(rendered_response));
        if(bytes_written < 0) {
            perror("Some error happened while writing repsonse to client\n");
        }else{
            printf("Response written!\n");
        }
        // Free the response
        free_response(res);
        free(rendered_response);

        // End the socket
        close(client_fd);

    } else {
        printf("404\n");
    }
}


char* http_render_response(Response* response){
    int buffer_len = 1; // space for \0 
    


    

    // Append version
    // Recalculate the size of buffer
    buffer_len += strlen(response->version);
    buffer_len += strlen(response->status_code);
    buffer_len += strlen(response->status_message);
    buffer_len += 2; // \r\n 
    buffer_len += 2; // for spaces


    // Add headers
    Header* current_header = response->headers;

    while(current_header != NULL){
        buffer_len += strlen(current_header->key);
        buffer_len += strlen(current_header->value);
        buffer_len += 2; // space blank and : chr size
        buffer_len += 2; // \r\n characters 

        current_header = current_header->next;
        
    }

    // Add body
    buffer_len += 4; // \r\n\r\n
    buffer_len += strlen(response->body);
    buffer_len += 2; // \r\n
    

    // Calculate Content-Length (length of the body only)
    char content_length_value_str[20];
    sprintf(content_length_value_str, "%zu", strlen(response->body));

    // Add Content-Type and Content-Length headers
    buffer_len += strlen("Content-Type: ") + strlen(mime_type_to_string(response->content_type)) + 2; // "\r\n"
    buffer_len += strlen("Content-Length: ") + strlen(content_length_value_str) + 2; // "\r\n"

    // Allocate buffer

    char* buffer = (char*)malloc(sizeof(char)* buffer_len);
        if (buffer == NULL) {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
    }

    buffer[0] = '\0'; // Initialize the buffer
    

    // put everything together

    // Status line
    strcat(buffer, response->version); 
    strcat(buffer, " "); 
    strcat(buffer, response->status_code); 
    strcat(buffer, " "); 
    strcat(buffer, response->status_message); 

    // Headers

    current_header = response->headers;

    while(current_header != NULL){
        strcat(buffer, current_header->key);
        strcat(buffer, ": ");
        strcat(buffer, current_header->value);
        strcat(buffer, "\r\n");

        current_header = current_header->next;
    }

    strcat(buffer, "\r\n");

    // Add Content-Type and Content-Length headers
    strcat(buffer, "Content-Type: ");
    strcat(buffer, mime_type_to_string(response->content_type));
    strcat(buffer, "\r\n");
    strcat(buffer, "Content-Length: ");
    strcat(buffer, content_length_value_str);
    strcat(buffer, "\r\n");

    // Add body

    strcat(buffer, "\r\n\r\n");
    strcat(buffer, response->body);
    strcat(buffer, "\r\n");


    

    return buffer;
}
void free_response(Response* response) {
    free(response->version);
    free(response->status_code);
    free(response->status_message);
    Header* current_header = response->headers;
    while (current_header != NULL) {
        Header* temp = current_header;
        current_header = current_header->next;
        free(temp->key);
        free(temp->value);
        free(temp);
    }
    free(response->body);
    free(response);
}

void print_response(Response* response) {
    // Print status line
    printf("%s %s %s\r\n", response->version, response->status_code, response->status_message);

    // Print headers
    Header* current_header = response->headers;
    while (current_header != NULL) {
        printf("%s: %s\r\n", current_header->key, current_header->value);
        current_header = current_header->next;
    }

    // Print a blank line to separate headers from the body
    printf("\r\n");

    // Print body
    printf("%s\r\n", response->body);
}

enum HttpMethods parse_method(char* method) {
    if (strcmp(method, "GET") == 0) return GET;
    if (strcmp(method, "POST") == 0) return POST;
    if (strcmp(method, "PUT") == 0) return PUT;
    if (strcmp(method, "DELETE") == 0) return DELETE;
    return UNKNOWN;
}
Request* http_parse_request(char* buffer) {
	
    // Allocate request
    Request* req = (Request*)malloc(sizeof(Request));
    if (!req) return NULL;
    
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
            char* headers_str = header_end + 2;  // The headers start after the first \r\n

            // Parse the status line
            char* method_end = strtok(status_line, " ");
            req->method = parse_method(method_end); // Parse method
            req->path = strdup(strtok(NULL, " "));  // Get path
            req->version = strdup(strtok(NULL, " ")); // Get version

            // Initialize headers
            req->headers = NULL;
            Header* current_header = NULL;

            // Parse header lines
            char* header_line = strtok(headers_str, "\r\n");

            while (header_line) {
                // Split each header into key and value
                char* colon_pos = strchr(header_line, ':');
                if (colon_pos) {
                    *colon_pos = '\0'; // Null-terminate the key
                    char* key = header_line;
                    char* value = colon_pos + 2; // Skip over ": "

                    // Create a new header
                    Header* new_header = (Header*)malloc(sizeof(Header));
                    new_header->key = strdup(key);
                    new_header->value = strdup(value);
                    new_header->next = NULL;

                    // Link it to the list
                    if (!req->headers) {
                        req->headers = new_header;
                    } else {
                        current_header->next = new_header;
                    }
                    current_header = new_header;
                }
                header_line = strtok(NULL, "\r\n");
            }

            // Copy body
            req->body = strdup(body);

        } else {
            printf("Error: Could not find status line delimiter.\n");
        }
    } else {
        printf("Error: Could not find header-body separator.\n");
    }
    
    return req;
}

void print_request(Request* request) {
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
    
    printf("Body: %s\n", request->body);

    // Print headers
    Header* current = request->headers;
    while (current) {
        printf("Header: %s: %s\n", current->key, current->value);
        current = current->next;
    }
}

void free_request(Request* request) {
    if (!request) return;

    // Free the allocated memory for each member
    free(request->path);
    free(request->version);
    
    Header* current_header = request->headers;
    while (current_header) {
        Header* temp = current_header;
        current_header = current_header->next;
        free(temp->key);
        free(temp->value);
        free(temp);
    }

    free(request->body);
    free(request);
}

void file_increment_counter() {
    int fd = open(COUNTER_PATH, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        perror("Open error");
        return;
    }

    // Lock the file to prevent concurrent writes
    if (flock(fd, LOCK_EX) < 0) {
        perror("Lock error");
        close(fd);
        return;
    }

    // Read the current counter value
    char buffer[64];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    int counter = 0;

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        counter = atoi(buffer);
    } else if (bytes_read < 0) {
        perror("Read error");
    }

    // Increment the counter
    counter++;

    // Write the updated counter back to the file
    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);  // Clear the file before writing
    snprintf(buffer, sizeof(buffer), "%d\n", counter);

    if (write(fd, buffer, strlen(buffer)) < 0) {
        perror("Write error");
    }

    // Unlock the file and close it
    flock(fd, LOCK_UN);
    close(fd);
}

void http_init(){

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	
    // Set the SO_REUSEADDR option
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt error");
        close(socket_fd);
        exit(-1);
    }

	// config address
    memset(&addr, 0, sizeof(addr));  // Clear out the struct
	addr.sin_port = htons(PORT);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	


	int bind_result = bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr));
	if(bind_result < 0 ){
		perror("Bind error");
		exit(-1);
	}

	int listen_result = listen(socket_fd, 10);
	if(listen_result < 0) {
		perror("Listen error");
		exit(-1);
	}
	
	printf("Listening on port %d\n", PORT);

	while(1){
		struct sockaddr_in client_addr;
		int client_addr_len = sizeof(client_addr); 
        int client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &client_addr_len);
		if(client_fd < 0){
			perror("Accept error");
			continue;
		}

		char* buffer = http_read_buffer(client_fd);

		if(buffer == NULL){
			perror("Buffer reading error");
			continue;
		}

		printf("Buffer: %s\n", buffer);


		Request* request = http_parse_request(buffer); 
		print_request(request);

		
		// Increment counter in every request
		file_increment_counter();

        // Route 
        http_route_request(request, client_fd);

		// Free
		free(request);
		free(buffer);
		
	}

}
char* http_read_buffer(int fd) {
    size_t buffer_size = INITIAL_BUFFER_SIZE;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        perror("Malloc error");
        return NULL;
    }

    size_t total_bytes_read = 0;
    ssize_t bytes_read;

    while (1) {
        // Read data into the buffer
        bytes_read = read(fd, buffer + total_bytes_read, buffer_size - total_bytes_read);

        // Check for errors or end of input
        if (bytes_read < 0) {
            perror("Read error");
            free(buffer);
            return NULL;
        } else if (bytes_read == 0) {
            break; // EOF
        }

        total_bytes_read += bytes_read;

        // Check if we need to resize the buffer
        if (total_bytes_read == buffer_size) {
            buffer_size *= 2;
            char *new_buffer = realloc(buffer, buffer_size);
            if (!new_buffer) {
                perror("Realloc error");
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
        }
    }

    // Null-terminate the buffer
    buffer[total_bytes_read] = '\0';

    return buffer;
}
int main(){
	http_init();
}
