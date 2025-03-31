# end 

file one web server with only 1500 lines of code.
includes socket, http, filelib and template engine.

## building
note: it only support POSIX api (Macos and Linux tested, but should work for other unix-like systems)

first install cmark to your system via https://github.com/commonmark/cmark?tab=readme-ov-file#installing
gcc main.c -o main $(pkg-config --cflags --libs libcmark)
./main

## Directories
"/public" for public files
"/notes" for posts
"/pages" for pages
"/source" for accessing to source code
