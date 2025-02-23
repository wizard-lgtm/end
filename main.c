
#include <time.h> 
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
#include <sys/stat.h>
#include <dirent.h>

#include <sys/fcntl.h>
#include <libgen.h>
#include <limits.h>

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
    size_t body_length;
} Response;

typedef struct Post{
    char* filename;
    int created;
    int modified;
    int private;
    struct Post* next;
} Post;

typedef struct TemplateData {
    char* key;
    char* value;
    struct TemplateData* next;
} TemplateData;


char* file_parse_extension(char* fpath){

    if(fpath == NULL) return NULL;

    // find the last '.'
    const char* dot = strrchr(fpath, '.');    

    // search dot if it's not found return null
    if (!dot || dot == fpath) return NULL; 


    // dot is now it's .{extension}.
    char* ext = strdup(dot);
    return ext;
    
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

enum HttpMimeTypes mime_from_string(char* fpath) {

    // Extract file extension
    char* ext = file_parse_extension(fpath);

    // Ensure extension is valid before comparing
    if (ext == NULL) {
        return MIME_APPLICATION_OCTET_STREAM; // Default MIME type for no extension
    }
    // Compare file extension
    if (strcmp(ext, ".html") == 0) return MIME_TEXT_HTML;
    if (strcmp(ext, ".txt") == 0) return MIME_TEXT_PLAIN;
    if (strcmp(ext, ".css") == 0) return MIME_TEXT_CSS;
    if (strcmp(ext, ".json") == 0) return MIME_APPLICATION_JSON;
    if (strcmp(ext, ".js") == 0) return MIME_APPLICATION_JAVASCRIPT;
    if (strcmp(ext, ".png") == 0) return MIME_IMAGE_PNG;
    if (strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".jpg") == 0) return MIME_IMAGE_JPEG;
    if (strcmp(ext, ".gif") == 0) return MIME_IMAGE_GIF;
    if (strcmp(ext, ".mp4") == 0) return MIME_VIDEO_MP4;

    free(ext);
    return MIME_APPLICATION_OCTET_STREAM;  // Default MIME type for unknown extensions
}




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
// Free the linked list
void free_posts(Post* head) {
    while (head) {
        Post* temp = head;
        head = head->next;
        free(temp->filename);
        free(temp);
    }
}
Post* read_dir_entries(char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        perror("opendir failed");
        return NULL;
    }

    struct dirent* entry;
    Post* head = NULL;
    Post* tail = NULL;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')  // Skip hidden files
            continue;

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        struct stat file_stat;
        if (stat(fullpath, &file_stat) == -1) {
            perror("stat failed");
            continue;
        }

        Post* new_post = malloc(sizeof(Post));
        if (!new_post) {
            perror("malloc failed");
            free_posts(head); // Free everything before returning
            closedir(dir);
            return NULL;
        }

        new_post->filename = strdup(entry->d_name);
        if (!new_post->filename) {  // Check strdup failure
            perror("strdup failed");
            free(new_post);
            free_posts(head);
            closedir(dir);
            return NULL;
        }

        new_post->created = (int)file_stat.st_ctime;
        new_post->modified = (int)file_stat.st_mtime;
        new_post->private = 0; 
        new_post->next = NULL;

        if (!head)
            head = new_post;
        else
            tail->next = new_post;
        
        tail = new_post;
    }

    closedir(dir);
    return head;
}

void print_posts(Post* head) {
    while (head) {
        printf("File: %s | Created: %d | Modified: %d | Private: %d\n",
               head->filename, head->created, head->modified, head->private);
        head = head->next;
    }
}

unsigned char* file_read_binary_to_buffer(const char* fpath, size_t* out_size) {
    FILE* file = fopen(fpath, "rb");
    if (!file) {
        printf("Failed to open file %s\n", fpath);
        return NULL;
    }

    // Seek to the end to get file size
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    // Allocate buffer
    unsigned char* buffer = (unsigned char*)malloc(size);
    if (!buffer) {
        fclose(file);
        perror("Memory allocation failed");
        return NULL;
    }

    // Read file content
    size_t bytesRead = fread(buffer, 1, size, file);
    fclose(file);

    if (bytesRead != size) {
        free(buffer);
        fprintf(stderr, "File read error: expected %zu bytes, got %zu bytes\n", size, bytesRead);
        return NULL;
    }

    if (out_size) {
        *out_size = size;
    }

    return buffer;
}



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

char* template_escape_html(const char* str) {
    size_t len = strlen(str);
    size_t new_len = len * 6 + 1; // Worst case: all chars are '&' -> "&amp;" (5 extra per char)
    char* escaped = malloc(new_len);
    if (!escaped) return NULL;

    char* out = escaped;
    for (const char* in = str; *in; in++) {
        switch (*in) {
            case '&': out += sprintf(out, "&amp;"); break;
            case '<': out += sprintf(out, "&lt;"); break;
            case '>': out += sprintf(out, "&gt;"); break;
            case '"': out += sprintf(out, "&quot;"); break;
            case '\'': out += sprintf(out, "&#39;"); break;
            default: *out++ = *in; break;
        }
    }
    *out = '\0';
    printf("Escaped html %s\n", escaped);
    return escaped;
}
int mark_double_down_calculate_html_size(const char* fcontent) {
    int size = strlen(fcontent); // Base size (assume same length initially)
    
    const char* src = fcontent;
    while (*src) {
        if (*src == '#' && (*(src + 1) == ' ' || *(src + 1) == '#')) {
            int level = 0;
            while (*src == '#') {
                level++;
                src++;
            }
            if (*src == ' ') {
                size += 7 + (level > 1 ? 2 * (level - 1) : 0); // <h1> + </h1> (7 extra), deeper levels add 2 extra per #
            }
        } else if (*src == '*' && *(src + 1) == '*') {
            size += 7; // **bold** → <b>bold</b> (adds 7 extra chars)
        } else if (*src == '*' && *(src + 1) != '*') {
            size += 7; // *italic* → <i>italic</i> (adds 7 extra chars)
        } else if (*src == '>' && *(src + 1) == ' ') {
            size += 17; // > text → <blockquote>text</blockquote> (adds 17 extra chars)
        } else if (*src == '`') {
            size += 11; // `code` → <code>code</code> (adds 11 extra chars)
        } else if (*src == '-' && *(src + 1) == '-' && *(src + 2) == '-') {
            size += 4; // --- → <hr> (adds 4 extra chars)
        } else if (*src == '[') {
            const char* temp = src;
            while (*temp && *temp != ']') temp++;
            if (*(temp + 1) == '(') { // Found a link [title](url)
                size += 15; // [title](url) → <a href="url">title</a> (approx. 15 extra chars)
            }
        } else if (*src == '!' && *(src + 1) == '[') {
            const char* temp = src;
            while (*temp && *temp != ']') temp++;
            if (*(temp + 1) == '(') { // Found an image ![alt](image.jpg)
                size += 15; // ![alt](image.jpg) → <img src="image.jpg" alt="alt"> (approx. 15 extra chars)
            }
        }
        src++;
    }
    
    return size;
}

/*
 * this code is written by ai 
 * but that's not mean i'm suck at programming i just gave the algorithm to ai
 * tell ur mom she has skill issues 
 */

 char* mark_double_down_parser(char* fpath) {
    // Read the file content into a buffer
    char* fcontent = file_read_to_buffer(fpath);
    if (!fcontent) {
        perror("Reading file error in mark_double_down_parser");
        return NULL;
    }

    // Calculate the required size for the HTML output
    int size = mark_double_down_calculate_html_size(fcontent);
    char* html = malloc(size);
    if (!html) {
        perror("Allocation error in mark_double_down_parser");
        free(fcontent);
        return NULL;
    }

    char* dest = html;      // Pointer to write into the output buffer
    const char* src = fcontent;  // Pointer to read from the input buffer

    // Process the input character by character
    while (*src) {
        // Detect Markdown-style headers (e.g., "# Header")
        if (*src == '#' && (*(src + 1) == ' ' || *(src + 1) == '#')) {
            int level = 0;
            while (*src == '#') { level++; src++; }
            
            if (*src == ' ') {
                dest += sprintf(dest, "<h%d>", level);
                src++; // Skip space

                // Parse heading content with inline formatting
                while (*src && *src != '\n') {
                    // Handle inline formatting (bold, italic, both)
                    if ((*src == '*' && *(src + 1) == '*') || (*src == '_' && *(src + 1) == '_')) {
                        // Bold formatting (**bold** or __bold__)
                        char delimiter = *src;
                        dest += sprintf(dest, "<strong>");
                        src += 2;
                        while (*src && !(*src == delimiter && *(src + 1) == delimiter)) {
                            *dest++ = *src++;
                        }
                        if (*src) {
                            dest += sprintf(dest, "</strong>");
                            src += 2;
                        }
                    } else if (*src == '*' || *src == '_') {
                        // Italic formatting (*italic* or _italic_)
                        char delimiter = *src;
                        dest += sprintf(dest, "<em>");
                        src++;
                        while (*src && *src != delimiter) {
                            *dest++ = *src++;
                        }
                        if (*src) {
                            dest += sprintf(dest, "</em>");
                            src++;
                        }
                    } else {
                        *dest++ = *src++;
                    }
                }
                dest += sprintf(dest, "</h%d>", level);
            }
        }
        // Detect blockquotes ("> Quote")
        else if (*src == '>' && *(src + 1) == ' ') {
            dest += sprintf(dest, "<blockquote>");
            src += 2;
            while (*src && *src != '\n') {
                *dest++ = *src++;
            }
            dest += sprintf(dest, "</blockquote>");
        }
        // Detect inline code (code)
        else if (*src == '`') {
            dest += sprintf(dest, "<code>");
            src++;
            while (*src && *src != '`') {
                *dest++ = *src++;
            }
            dest += sprintf(dest, "</code>");
            src++;
        }
        // Bold + Italic (*** or ___)
        else if ((src[0] == '*' && src[1] == '*' && src[2] == '*') || 
                 (src[0] == '_' && src[1] == '_' && src[2] == '_')) {
            char delimiter = *src;
            dest += sprintf(dest, "<strong><em>");
            src += 3;
            while (*src && !(*src == delimiter && src[1] == delimiter && src[2] == delimiter)) {
                *dest++ = *src++;
            }
            if (*src) {
                dest += sprintf(dest, "</em></strong>");
                src += 3;
            }
        }
        // Bold (** or __)
        else if ((src[0] == '*' && src[1] == '*') || (src[0] == '_' && src[1] == '_')) {
            char delimiter = *src;
            dest += sprintf(dest, "<strong>");
            src += 2;
            while (*src && !(*src == delimiter && src[1] == delimiter)) {
                *dest++ = *src++;
            }
            if (*src) {
                dest += sprintf(dest, "</strong>");
                src += 2;
            }
        }
        // Italics (* or _)
        else if (*src == '*' || *src == '_') {
            char delimiter = *src;
            dest += sprintf(dest, "<em>");
            src++;
            while (*src && *src != delimiter) {
                *dest++ = *src++;
            }
            if (*src) {
                dest += sprintf(dest, "</em>");
                src++;
            }
        }
        // Links: [text](url)
        else if (*src == '[') {
            const char* link_text_start = ++src; // Skip '['
            while (*src && *src != ']') src++; // Find closing ']'
            
            if (*src == ']' && *(src + 1) == '(') {
                size_t text_length = src - link_text_start; // Length of link text
                src += 2; // Skip "]("
                const char* url_start = src;
                
                while (*src && *src != ')') src++; // Find closing ')'
                
                if (*src == ')') {
                    size_t url_length = src - url_start; // Length of URL
                    src++; // Skip past ')'
                    
                    // Allocate temporary buffers for text and URL
                    char* link_text = strndup(link_text_start, text_length);
                    char* link_url = strndup(url_start, url_length);
                    
                    // Append formatted link to the output
                    dest += sprintf(dest, "<a href=\"%s\">%s</a>", link_url, link_text);
                    
                    // Free temporary buffers
                    free(link_text);
                    free(link_url);
                }
            } else {
                *dest++ = '['; // Invalid format, treat as normal text
                src = link_text_start;
            }
        }
        // Markdown image support: ![alt text](image URL)
        else if (*src == '!' && *(src + 1) == '[') {
            const char* alt_text_start = src + 2; // Skip "!["
        
            // Find the closing ']'
            while (*src && *src != ']') src++; 
            
            if (*src == ']') {
                size_t alt_text_length = src - alt_text_start; // Length of alt text
                src++; // Skip past ']'
                
                // Check if there's an opening '(' after the ']'
                if (*src == '(') {
                    const char* url_start = ++src; // Skip past '('
        
                    // Find the closing ')'
                    while (*src && *src != ')') src++; 
        
                    if (*src == ')') {
                        size_t url_length = src - url_start; // Length of URL
                        src++; // Skip past ')'
                        
                        // Allocate temporary buffers for alt text and URL
                        char* alt_text = strndup(alt_text_start, alt_text_length);
                        char* image_url = strndup(url_start, url_length);
                        
                        // Append formatted image tag to the output
                        dest += sprintf(dest, "<img src=\"%s\" alt=\"%s\">", image_url, alt_text);
                        
                        // Free temporary buffers
                        free(alt_text);
                        free(image_url);
                    }
                }
            }
        }
        
        // Default case: copy character as-is
        else {
            *dest++ = *src++;
        }
    }

    *dest = '\0'; // Null-terminate the generated HTML string
    free(fcontent); // Free the input buffer
    return html; // Return the converted HTML
}


typedef struct FileStats {
    char* created;
    char* modified;
    char* filename;
    char* path; // absolute path
} FileStats;
typedef struct FileList {
    struct FileStats* stats;
    struct FileList* next;
} FileList;

void print_file_list(FileList* head) {
    FileList* current = head;
    
    if (current == NULL) {
        printf("The file list is empty.\n");
        return;
    }
    
    printf("File List:\n");
    printf("--------------------------------------------------\n");
    
    while (current != NULL) {
        printf("Filename: %s\n", current->stats->filename);
        printf("Created: %s\n", current->stats->created);
        printf("Modified: %s\n", current->stats->modified);
        printf("--------------------------------------------------\n");
        
        current = current->next;
    }
}

void file_format_date(time_t raw_time, char *date_str) {
    struct tm *time_info = localtime(&raw_time);
    strftime(date_str, 11, "%d/%m/%Y", time_info); // Format: dd/mm/yyyy
}

int file_calculate_days_diff(time_t raw_time) {
    int SECONDS_IN_A_DAY = 86400;

    time_t current_time = time(NULL);
    return (int)difftime(current_time, raw_time) / SECONDS_IN_A_DAY;
}

FileStats* file_get_file_stats(char* fpath){
    printf("fpath: %s\n", fpath);
    // Allocate struct 
    FileStats* file_stats = (FileStats*)malloc(sizeof(FileStats));
    if (!file_stats) {
        perror("Memory allocation for FileStats failed");
        return NULL;
    }

    struct stat file_stat;
    // Get file stats
    if (stat(fpath, &file_stat) == -1) {
        perror("Unable to get file stats");
        return NULL;
    }

    // Allocate and format creation date
    file_stats->created = (char*)malloc(11 * sizeof(char));
    file_format_date(file_stat.st_ctime, file_stats->created);

    // Allocate and format modification date
    file_stats->modified = (char*)malloc(11 * sizeof(char));
    file_format_date(file_stat.st_mtime, file_stats->modified);

    // extract basename of path
    char *filename = basename(fpath); 
    file_stats->filename = strdup(filename);

     // Add absolute path
     char *abs_path = realpath(fpath, NULL);
     if (abs_path == NULL) {
         perror("Failed to get absolute path");
         file_stats->path = strdup(fpath); // Fall back to original path
     } else {
         file_stats->path = abs_path; // Transfer ownership (no need to strdup)
     }

    return file_stats;
}

FileList* file_list_dir(char* fpath){
    FileList* head = NULL;
    FileList* tail = NULL;

    
    // Open dir
    DIR* dir = opendir(fpath);
    if(!dir){
        printf("Error: can't open folder %s\n", fpath);
        return NULL;
    }
    struct dirent* entry;

    while((entry = readdir(dir)) != NULL){
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        int full_path_len = strlen(fpath) + strlen(entry->d_name) + 2;
        char full_path[full_path_len]; // max path long
        snprintf(full_path, full_path_len, "%s/%s", fpath, entry->d_name);

        FileStats* stats = file_get_file_stats(full_path);
        if(!stats){
            continue;
        }

        stats->path = full_path;

        printf("Absolute path: %s\n", full_path);
        
        FileList* new_node = (FileList*)malloc(sizeof(FileList));
        if(!new_node){
            perror("Allocation failed on file_list_dir");
            return NULL;
        }
        new_node->stats = stats;
        new_node->next = NULL;

        if(!head){
            head = new_node;
        }else{
            tail->next = new_node;
        }
    
        tail = new_node;
    }

    closedir(dir);

    return head;

}



void file_free_file_stats(FileStats* file_stats) {
    if (file_stats) {
        free(file_stats->created);
        free(file_stats->modified);
        free(file_stats->filename);
        free(file_stats->path);
        free(file_stats);
    }
}

void file_free_file_list(FileList* file_list){
    while(file_list != NULL){
        FileList* temp = file_list;
        file_list = file_list->next;
        file_free_file_stats(temp->stats);
        free(temp);
    }
}

// Function to look up a variable in the linked list
char* template_lookup_variable(TemplateData* data, const char* key) {
    if(!data){
        printf("No template data found!\n");
    }
    while (data) {
        printf("%s == %s?\n", data->key, key);
            // Traverse the linked list
        if (strcmp(data->key, key) == 0) {
            return data->value;
        }
        data = data->next;
    }
    return "UNKNOWN";  // Default if variable not found
}

// Function to add a variable to the linked list
void template_add_variable(TemplateData** head, const char* key, const char* value) {
    TemplateData* new_node = (TemplateData*)malloc(sizeof(TemplateData));
    new_node->key = strdup(key);
    new_node->value = strdup(value);
    new_node->next = *head;
    *head = new_node;
}

// Function to free the linked list
void template_free_list(struct TemplateData* head) {
    while (head) {
        TemplateData* temp = head;
        head = head->next;
        free(temp->key);
        free(temp->value);
        free(temp);
    }
}

char* template_render_template(char* fpath, TemplateData* data) {
    // Open file 
    char* buffer = file_read_to_buffer(fpath);
    if (!buffer) {
        perror("Can't read template buffer");
        return strdup("<h1>500 Internal Server Error</h1><p>Failed to load template file.</p>");
    }

    char* start;
    char* end;
    int size = strlen(buffer) + 1;  // Base size
    char* rendered_html = malloc(size);
    if (!rendered_html) {
        perror("Memory allocation failed");
        free(buffer);
        return strdup("<h1>500 Internal Server Error</h1><p>Memory allocation failed.</p>");
    }

    int out_index = 0;
    char* current = buffer;
    int line = 1; // Track line number
    // Find start of template variable

    // Decide it's raw or html {raw values are _(), html values are *_()}
    // Escape html if it's raw 
    int escape_html;
    while (1) {
        char* raw_start = strstr(current, "_(");
        char* html_start = strstr(current, "*_(");
    
        if (!raw_start && !html_start) break; // No more placeholders
    
        if (!raw_start || (html_start && html_start < raw_start)) {
            start = html_start;
            escape_html = 1;
        } else {
            start = raw_start;
            escape_html = 0;
        }
    
        // Count lines until this point
        for (char* temp = current; temp < start; temp++) {
            if (*temp == '\n') line++;
        }
    
        // Copy content before the placeholder
        size_t chunk_size = start - current;
        memcpy(rendered_html + out_index, current, chunk_size);
        out_index += chunk_size;
    
        end = strchr(start, ')');  // Find closing bracket
        if (!end) {
            fprintf(stderr, "Syntax error in template at line %d: missing closing `)`\n", line);
            free(buffer);
            free(rendered_html);
            return strdup("<h1>500 Internal Server Error</h1><p>Syntax error in template</p>");
        }
    
        // Extract variable name safely
        char var_name[64] = {0};
        size_t var_length = end - start - (escape_html ? 3 : 2);
    
        if (var_length >= sizeof(var_name)) {
            fprintf(stderr, "Template error at line %d: Variable name too long\n", line);
            free(buffer);
            free(rendered_html);
            return strdup("<h1>500 Internal Server Error</h1><p>Variable name too long.</p>");
        }
    
        memcpy(var_name, start + (escape_html ? 3 : 2), var_length);
        var_name[var_length] = '\0';  // Null-terminate properly
    
        // Lookup variable
        printf("Looking up for variable name: %s\n", var_name);
        char* raw_value = template_lookup_variable(data, var_name);
        char* value = escape_html ? template_escape_html(raw_value) : strdup(raw_value);
    
        if (!value) {
            free(buffer);
            free(rendered_html);
            return strdup("<h1>500 Internal Server Error</h1><p>Memory allocation failed.</p>");
        }
    
        size_t value_len = strlen(value);
    
        // Resize if needed
        if (out_index + value_len >= size) {
            size += value_len + 64;
            rendered_html = realloc(rendered_html, size);
            if (!rendered_html) {
                perror("Memory reallocation failed");
                free(buffer);
                free(value);
                return strdup("<h1>500 Internal Server Error</h1><p>Memory reallocation failed.</p>");
            }
        }
    
        // Copy the variable's value
        memcpy(rendered_html + out_index, value, value_len);
        out_index += value_len;
    
        free(value);  // Free allocated memory for escaped value
        current = end + 1; // Move past the variable
    } 

    // Copy remaining part of the string


    // Ensure there's enough space in rendered_html for the remaining string
    size_t remaining_space = size - out_index;
    if (remaining_space > 0) {
        // Copy remaining part of the string safely using snprintf
        int written = snprintf(rendered_html + out_index, remaining_space, "%s", current);
        
        // If snprintf truncates the string, realloc the buffer
        if (written >= remaining_space) {
            // Increase size to accommodate the entire string
            size += written + 1;  // Extra space for null terminator
            rendered_html = realloc(rendered_html, size);
            if (!rendered_html) {
                perror("Memory reallocation failed");
                free(buffer);
                return strdup("<h1>500 Internal Server Error</h1><p>Memory reallocation failed.</p>");
            }

            // Copy the remaining part of the string after realloc
            snprintf(rendered_html + out_index, size - out_index, "%s", current);
        }
    } else {
        fprintf(stderr, "Not enough space in buffer to copy the remaining string.\n");
    }

    free(buffer);
    return rendered_html;
}

void http_route_500(Request* req, Response* res){
    res->version = strdup("HTTP/1.1");
    res->status_code = strdup("500");
    res->status_message = strdup("INTERNAL SERVER ERROR");
    res->body = strdup("<h1> 500 - Internal Server Error </h1>");
    res->headers = NULL;
}

void http_route_404(Request* req, Response* res){
        res->version = strdup("HTTP/1.1");
        res->status_code = strdup("404");
        res->status_message = strdup("NOT FOUND");

        char* page = file_read_to_buffer("./pages/404.html");
        res->body = strdup(page);
        res->body_length = strlen(page);
        res->content_type = MIME_TEXT_HTML;
        printf("body length%d\n", res->body_length);
        if (res->body == NULL) {
            perror("Failed to allocate memory for response body");
        }
        res->headers = NULL; 
        free(page);
}



void http_route_source(Request* req, Response* res){

        res->version = strdup("HTTP/1.1");
        res->status_code = strdup("200");
        res->status_message = strdup("OK");
        res->content_type = MIME_TEXT_HTML;

        char* src = file_read_to_buffer("./main.c");
        FileStats* stats = file_get_file_stats("./main.c");

        TemplateData* data = NULL;
        template_add_variable(&data, "code", src);
        template_add_variable(&data, "created", stats->created);
        template_add_variable(&data, "modified", stats->modified);
        char* page = template_render_template("./pages/source.html", data);
        res->body = page; 
        if (res->body == NULL) {
            perror("Failed to allocate memory for response body");
            free(src);
            file_free_file_stats(stats);
            http_route_500(req,res);
        }
        free(src);
        file_free_file_stats(stats);
}


void http_route_notes(Request* req, Response* res) {
    char* fpath = malloc(strlen(req->path) + 1);
    if (fpath == NULL) {
        perror("Failed to allocate memory for fpath");
        return;
    }
    strcpy(fpath, req->path);
    printf("Requested post: %s\n", fpath);

    const char* ext = strrchr(req->path, '.');
    int is_html = 0;
    if (ext && strcmp(ext, ".html") == 0) {
        is_html = 1;
    }

    size_t total_size = 2 + strlen(req->path); // "." + path + NULL terminator
    char* filename = (char*)malloc(total_size);
    if (!filename) {
        free(fpath);
        http_route_500(req, res);
        return;
    }
    snprintf(filename, total_size, ".%s", req->path);
    printf("Filename: %s\n", filename);

    FileStats* fstats = file_get_file_stats(filename);
    if (!fstats) {
        free(filename);
        free(fpath);
        http_route_404(req, res);
        return;
    }

    char* fcontent = file_read_to_buffer(filename);
    if (!fcontent) {
        file_free_file_stats(fstats);
        free(filename);
        free(fpath);
        http_route_404(req, res);
        return;
    }

    if (!is_html) {
        char* markdown = mark_double_down_parser(filename);
        if (!markdown) {
            free(fcontent);
            file_free_file_stats(fstats);
            free(filename);
            free(fpath);
            http_route_404(req, res);
            return;
        }

        TemplateData* data = NULL;
        template_add_variable(&data, "content", markdown);
        template_add_variable(&data, "filename", fstats->filename);
        template_add_variable(&data, "created", fstats->created);
        template_add_variable(&data, "modified", fstats->modified);

        char* rendered_html = template_render_template("./pages/note.html", data);

        res->body = strdup(rendered_html);
        template_free_list(data);
        free(rendered_html);
        free(markdown);
    } else {
        res->body = strdup(fcontent);
    }

    res->version = strdup("HTTP/1.1");
    res->status_code = strdup("200");
    res->status_message = strdup("OK");
    res->body_length = strlen(res->body);
    res->content_type = MIME_TEXT_HTML;

    free(fcontent);
    file_free_file_stats(fstats);
    free(filename);
    free(fpath);
}


char* http_render_file_list(FileList* list){
    int size = 1; // \0
    char* buffer = malloc(size);
    buffer[0] = '\0';

    char* fmt = "<a href=\"/post/%s\">filename: %s</a></br>";

    while(list != NULL){
        char* fname = list->stats->filename;
        int needed = snprintf(NULL, 0, fmt, fname, fname) + 1;
        char* new_buffer = realloc(buffer, size + needed);
        if (!new_buffer) {
            free(buffer); 
            return NULL;
        }
        buffer = new_buffer;

        sprintf(buffer + size - 1, fmt, fname, fname); // at that point i don't even know what am i writing

        size += needed - 1;
        list = list->next;
    }

    return buffer;
    
}

void http_route_home(Request* req, Response* res){
    res->version = strdup("HTTP/1.1");
    res->status_code = strdup("200");
    res->status_message = strdup("OK");
    res->content_type = MIME_TEXT_HTML;

    char* posts_path = "./post";
    FileList* list = file_list_dir(posts_path);
    if(!list){
        http_route_500(req, res);
    }

    print_file_list(list);
    

    TemplateData* data = NULL;
    char* posts_html = http_render_file_list(list);
    template_add_variable(&data, "posts", posts_html);

    char* body = template_render_template("./pages/index.html", data);
    if (!body) {
        perror("template_render_template failed");
        template_free_list(data);
       
        http_route_500(req, res);
        return;
    }

    template_free_list(data);

    res->body = strdup(body);
    res->body_length = strlen(body);

    if (!res->body) {
        perror("Failed to allocate memory for response body");
        free(body);
       
        http_route_500(req, res);
        return;
    }

    res->headers = NULL;

    free(posts_html);
    free(body);
  
}



void http_route_public(Request* req, Response* res) {
    char* fpath = malloc(strlen(req->path) + 1);
    if (fpath == NULL) {
        perror("Failed to allocate memory for fpath");

        return;
    }
    strcpy(fpath, req->path);
    
    if (strncmp(fpath, "/public", 7) != 0) {
        free(fpath);
        http_route_404(req, res);
        return;
    }
    
    memmove(fpath, fpath + 7, strlen(fpath) - 6);
    res->version = strdup("HTTP/1.1");
    res->status_code = strdup("200");
    res->status_message = strdup("OK");
    res->headers = NULL;
    
    enum HttpMimeTypes mime_type = mime_from_string(fpath);
    printf("mime type %s\n", mime_type_to_string(mime_type));
    res->content_type = mime_type;
    
    char* full_path = malloc(strlen("./public") + strlen(fpath) + 1);
    strcpy(full_path, "./public");
    strcat(full_path, fpath);  
    
    size_t size;
    char* file = file_read_binary_to_buffer(full_path, &size);
    if(file) {
        res->body = malloc(size);
        if(res->body) {
            memcpy(res->body, file, size);
            res->body_length = size;
        }
    }
    
    free(fpath);
    free(full_path); 
    free(file);
}
Response* http_route_request(Response* response, Request* request) {
    char* path = request->path;
    if((strcmp(path, "/public") == 0) || (strncmp(path, "/public/", 8) == 1) || strcmp(path, "/test") == 1){
        // Increment counter if it's not 404 and public request  
        file_increment_counter();
    }
    if (response == NULL) {
        perror("Failed to allocate memory for response");
    }
    if (strcmp(path, "/") == 0) {
        http_route_home(request, response);
    }
    else if(strcmp(path, "/source") == 0){
        http_route_source(request, response);
    }

    else if ((strcmp(path, "/post") == 0) || (strncmp(path, "/post/", 6) == 0)) {
        http_route_notes(request, response);
    } 

    else if ((strcmp(path, "/public") == 0) || (strncmp(path, "/public/", 8) == 0)) {
        http_route_public(request, response);
    }
    

    // Check if it's starts first "/public" (do not change original path)
    // http_route_public
    
    else {
        http_route_404(request, response);
    }

    return response;
}

void http_complete_connection(struct timespec start, Response* response, int client_fd) {
    // Get current time with nanosecond precision
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double render_time = (now.tv_sec - start.tv_sec) * 1000.0 + 
                        (now.tv_nsec - start.tv_nsec) / 1000000.0;
    
    if(
        response->content_type == MIME_TEXT_HTML || 
        response->content_type == MIME_TEXT_PLAIN || 
        response->content_type == MIME_TEXT_CSS
    ){
        // Append the body length if it's not 
        if(!response->body_length){
            response->body_length = strlen(response->body);
        }
    }

    // Set unknown mime if it's not set for security
    if(response->content_type == UNKNOWN){
        printf("Mime type is unknown! Setting to octet-stream\n");
        response->content_type = MIME_APPLICATION_OCTET_STREAM;
    }

    // Only append timing for text responses
    if (
        response->content_type == MIME_TEXT_HTML || 
        response->content_type == MIME_TEXT_PLAIN  
        ) {
        
        // For text responses, we can append timing
        char render_time_str[64];
        snprintf(render_time_str, sizeof(render_time_str), 
                "\nConnection completed in %.3f ms\n %ld bytes of body written\n", render_time, response->body_length);
        
        // Calculate new size and reallocate
        size_t timing_len = strlen(render_time_str);
        size_t new_len = response->body_length + timing_len;
        char* new_body = malloc(response->body_length + timing_len + 1);  // Add 1 for null terminator
        if (new_body) {
            memcpy(new_body, response->body, response->body_length);
            memcpy(new_body + response->body_length, render_time_str, timing_len);
            new_body[response->body_length + timing_len] = '\0';  // Null-terminate
            free(response->body);
            response->body = new_body;
            response->body_length += timing_len;
        }
    }

    // Render the response
    char* rendered_response = http_render_response(response);
    if (!rendered_response) {
        perror("Failed to render response");
        close(client_fd);
        return;
    }

    // Calculate total response size (headers + body + extra)
    size_t total_size = response->body_length;  // body size
    total_size += strlen(response->version) + strlen(response->status_code) + 
                 strlen(response->status_message) + 4;  // status line
    
    // Add headers size
    Header* current_header = response->headers;
    while (current_header != NULL) {
        total_size += strlen(current_header->key) + strlen(current_header->value) + 4;
        current_header = current_header->next;
    }
    
    // Add Content-Type and Content-Length headers size
    char content_length_str[32];
    snprintf(content_length_str, sizeof(content_length_str), "%zu", response->body_length);
    total_size += strlen("Content-Type: ") + 
                 strlen(mime_type_to_string(response->content_type)) + 2;
    total_size += strlen("Content-Length: ") + strlen(content_length_str) + 2;
    
    // Add extra newlines and null terminator
    total_size += 6;  // \r\n\r\n + final \r\n + \0

    // Write the complete response
    ssize_t bytes_written = write(client_fd, rendered_response, total_size);
    if (bytes_written < 0) {
        perror("Error writing response to client");
    } else {
        printf("Response written successfully!, %zd bytes written\n", bytes_written);
    }

    free(rendered_response);
    close(client_fd);
}
char* http_render_response(Response* response) {
    int buffer_len = 1; // space for \0 

    // Append version
    buffer_len += strlen(response->version);
    buffer_len += strlen(response->status_code);
    buffer_len += strlen(response->status_message);
    buffer_len += 4; // space + space + \r\n
    
    // Add headers
    Header* current_header = response->headers;
    while(current_header != NULL) {
        buffer_len += strlen(current_header->key);
        buffer_len += strlen(current_header->value);
        buffer_len += 4; // ": " and \r\n
        current_header = current_header->next;
    }
    
    // Add body
    buffer_len += 4; // \r\n\r\n
    buffer_len += response->body_length;  // Use stored length instead of strlen
    buffer_len += 2; // final \r\n
    
    // Convert content length to string
    char content_length_value_str[32];
    sprintf(content_length_value_str, "%zu", response->body_length);
    
    // Add Content-Type and Content-Length headers
    buffer_len += strlen("Content-Type: ") + strlen(mime_type_to_string(response->content_type)) + 2;
    buffer_len += strlen("Content-Length: ") + strlen(content_length_value_str) + 2;
    // Allocate buffer
    char* buffer = malloc(buffer_len);
    if (buffer == NULL) {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
    }
    
    // Use pointer arithmetic for building the response
    char* ptr = buffer;
    
    // Status line
    ptr += sprintf(ptr, "%s %s %s\r\n", response->version, response->status_code, response->status_message);
    
    // Headers
    current_header = response->headers;
    while(current_header != NULL) {
        ptr += sprintf(ptr, "%s: %s\r\n", current_header->key, current_header->value);
        current_header = current_header->next;
    }
    
    // Content-Type and Content-Length headers
    ptr += sprintf(ptr, "Content-Type: %s\r\n", mime_type_to_string(response->content_type));
    ptr += sprintf(ptr, "Content-Length: %s\r\n", content_length_value_str);
    
    // Blank line
    memcpy(ptr, "\r\n", 2);
    ptr += 2;
    
    // Body
    memcpy(ptr, response->body, response->body_length);
    ptr += response->body_length;
    
    // Final \r\n
    memcpy(ptr, "\r\n", 2);
    ptr += 2;
    *ptr = '\0';
    
    return buffer;
}
Response* http_response_init() {
    Response* res = (Response*)malloc(sizeof(Response));
    if(!res){
        perror("Can't allocate space for response!\n");
        return NULL;
    }
    res->version = strdup("HTTP/1.1");
    res->status_code = strdup("200");
    res->status_message = strdup("OK");
    res->content_type = UNKNOWN;
    res->body = NULL;
    res->body_length = 0;
    res->headers = NULL;
    return res;
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



		Request* request = http_parse_request(buffer); 
        if(!request){
            printf("probably malformed request idk\n");
            close(client_fd);
            continue;
        }
        Response* response = http_response_init();
        if(!response){
            perror("Alloc error!\n");
                const char *error_response =
        "HTTP/1.1 500 Internal Server Error\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 21\r\n"
        "\r\n"
        "500 Internal Server Error";

        write(client_fd, error_response, strlen(error_response));
        }

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
    buffer[total_read] = 0;
    return buffer;
}
int main(){
	http_init();
}
