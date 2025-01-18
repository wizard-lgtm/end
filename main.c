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


#define PORT 8000
#define INITIAL_BUFFER_SIZE 1024
#define COUNTER_PATH "counter.txt"
 
typedef struct Header {
    char* key;
    char* value;
    struct HttpHeader* next;
} HttpHeader;
enum HttpMethods{
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
    HttpHeader* headers;  // Changed to linked list of headers
    char* body;
} Request;
typedef struct {
	char* version;
	int status_code;
	char* status_message;
	char* headers;
	char* body;
} Response;



char* http_read_buffer(int fd);
Request* http_parse_request(char* buffer);
Response* http_response_init();
void http_route_request();
char* http_render_response(Response* response);
void http_write_buffer(char* buffer);

void free_request(Request*);


int socket_fd;
struct sockaddr_in addr;

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
	// Parse first line

	char* method_str = strtok(buffer, " ");  // First part: Method
    char* path = strtok(NULL, " ");          // Second part: Path
    char* version = strtok(NULL, "\r\n");    // Third part: HTTP version


	req->method = parse_method(method_str);
    req->path = strdup(path);
    req->version = strdup(version);

    return req;
}

void print_request(const Request* request) {
    if (!request) {
        printf("Invalid request.\n");
        return;
    }

    const char* method;
    switch ((request->method)) {
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
		int client_fd = accept(socket_fd, (struct sockaddr*)&addr, &client_addr_len);
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
