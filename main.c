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
#include <time.h>


#define PORT 8001
#define BUFFER_CHUNK_SIZE 1024
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



char* http_read_buffer(int fd);
Request* http_parse_request(char* buffer);
Response* http_route_connection(Request* request, Response* response);
char* http_render_response(Response* response);
void http_write_buffer(char* buffer);
void print_response(Response*);
void free_response(Response*);
void free_request(Request*);
char* read_file_to_buffer(char* fpath);

int socket_fd;
struct sockaddr_in addr;

char* file_read_to_buffer(char* fpath) {
    FILE* file = fopen(fpath, "rb");
    if (!file) {
        perror("Failed to open file");
        return NULL;
    }

    // Seek to the end to get file size
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);  

    // Allocate buffer
    char* buffer = (char*)malloc(size + 1); // +1 for null terminator
    if (!buffer) {
        fclose(file);
        perror("Memory allocation failed");
        return NULL;
    }

    // Read file content
    fread(buffer, 1, size, file);
    buffer[size] = '\0';  // Null-terminate the buffer

    fclose(file);

    return buffer;
}

void http_route_home(Request* req, Response* res){
        res->version = strdup("HTTP/1.1");
        res->status_code = strdup("200");
        res->status_message = strdup("OK");

        char* body = file_read_to_buffer("./pages/index.html");
        res->body = strdup(body);
        if (res->body == NULL) {
            perror("Failed to allocate memory for response body");
            exit(EXIT_FAILURE);
        }
        res->headers = NULL; 
        free(body);
}

void http_route_404(Request* req, Response* res){
        res->version = strdup("HTTP/1.1");
        res->status_code = strdup("404");
        res->status_message = strdup("NOT FOUND");

        char* page = file_read_to_buffer("./pages/404.html");
        res->body = strdup(page);
        if (res->body == NULL) {
            perror("Failed to allocate memory for response body");
            exit(EXIT_FAILURE);
        }
        res->headers = NULL; 
        free(page);
}

Response* http_route_request(Response* response, Request* request) {
    char* path = request->path;

    if (response == NULL) {
        perror("Failed to allocate memory for response");
    }
    if (strcmp(path, "/") == 0) {

        http_route_home(request, response);
    } else {
        http_route_404(request, response);
    }

    return response;
}

void http_complete_connection(struct timespec start, Response* response, int client_fd) {
    // Get current time with nanosecond precision
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    // Compute elapsed time in milliseconds with fractional precision
    double render_time = (now.tv_sec - start.tv_sec) * 1000.0 + (now.tv_nsec - start.tv_nsec) / 1000000.0;

    // Allocate buffer for render time string
    char render_time_str[64];
    snprintf(render_time_str, sizeof(render_time_str), "\nRendered in %.3f ms\n", render_time);

    printf("%s\n", render_time_str);

    // Modify response body before rendering
    size_t new_body_length = strlen(response->body) + strlen(render_time_str) + 1;
    char* new_body = malloc(new_body_length);
    if (!new_body) {
        perror("Memory allocation failed");
        close(client_fd);
        return;
    }

    // Concatenate response body and render time message
    strcpy(new_body, response->body);
    strcat(new_body, render_time_str);

    // Free the old body and update response->body
    free(response->body);
    response->body = new_body;

    // Render the final response
    char* rendered_response = http_render_response(response);
    printf("Response str is:\n%s\n", rendered_response);

    // Send response to client
    int bytes_written = write(client_fd, rendered_response, strlen(rendered_response));
    if (bytes_written < 0) {
        perror("Error writing response to client");
    } else {
        printf("Response written successfully!\n");
    }

    // Free the rendered response
    free(rendered_response);

    // Close the socket
    close(client_fd);
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
    sprintf(content_length_value_str, "%zu", strlen(response->body)+3);

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

    printf("request buffer: %s\n", buffer);
	
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
        return NULL;
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
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);

		char* buffer = http_read_buffer(client_fd);

		if(buffer == NULL){
			perror("Buffer reading error");
			continue;
		}

		printf("Buffer: %s\n", buffer);

		// Increment counter in every request
		file_increment_counter();

		Request* request = http_parse_request(buffer); 
        if(!request){
            printf("probably malformed request idk\n");
            close(client_fd);
            continue;
        }
        Response* response = (Response*)malloc(sizeof(Response));

        // Route 
        http_route_request(response, request);



        http_complete_connection(start, response, client_fd);

		// Free
        free_response(response);
        free_request(request);
		free(buffer);
		
	}

}
char* http_read_buffer(int fd){
    int chunk_count = 1;
    int total_size = 0;
    int total_read = 0;
    int bytes_read = 0;
    char* buffer = NULL;
    total_size = BUFFER_CHUNK_SIZE;
    buffer = malloc(total_size);
    while(1){
        bytes_read = read(fd, buffer + total_read, BUFFER_CHUNK_SIZE);
        if(bytes_read == 0){
            break;
        } 
        total_read += bytes_read;

        if(total_read >= total_size){
            chunk_count++;
            total_size = BUFFER_CHUNK_SIZE * chunk_count;
            char* new_buffer = realloc(buffer, total_size);
            
            buffer = new_buffer;
        }else{
            break;
        }
    }
    buffer[total_size] = 0;
    return buffer;
}
int main(){
	http_init();
}
