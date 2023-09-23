#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define PORT 8080
#define FILENAME "sample.txt"
#define SECRET_KEY 'K'  // Change this to your secret key

// Structure to store file metadata
struct FileMetadata {
    off_t size;
    time_t mtime;
};

// XOR encryption/decryption function
void xor_encrypt_decrypt(char *data, size_t len, char key) {
    for (size_t i = 0; i < len; ++i) {
        data[i] ^= key;
    }
}

// Function to send a portion of a file
void send_file_chunk(FILE* file, size_t offset, size_t size, int client_socket, char key) {
    fseek(file, offset, SEEK_SET);
    char buffer[1024];
    size_t total_sent = 0;

    while (total_sent < size) {
        size_t bytes_to_send = sizeof(buffer);
        if (size - total_sent < bytes_to_send) {
            bytes_to_send = size - total_sent;
        }

        size_t bytes_read = fread(buffer, 1, bytes_to_send, file);
        xor_encrypt_decrypt(buffer, bytes_read, key);
        send(client_socket, buffer, bytes_read, 0);

        total_sent += bytes_read;
    }
}

// Function to get file metadata
struct FileMetadata get_file_metadata(const char* filename) {
    struct stat file_stat;
    struct FileMetadata metadata;

    if (stat(filename, &file_stat) == 0) {
        metadata.size = file_stat.st_size;
        metadata.mtime = file_stat.st_mtime;
    } else {
        metadata.size = -1;
        metadata.mtime = -1;
    }

    return metadata;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Initialize server address struct
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket to the server address
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, 5) == -1) {
        perror("Error listening");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        // Accept a client connection
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            perror("Error accepting client connection");
            continue;
        }

        printf("Client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Get file metadata of the server's file
        struct FileMetadata server_metadata = get_file_metadata(FILENAME);

        if (server_metadata.size == -1) {
            perror("Error getting file metadata");
            close(client_socket);
            continue;
        }

        // Send the file size and modification time to the client
        send(client_socket, &server_metadata, sizeof(server_metadata), 0);

        // Receive the client's file metadata
        struct FileMetadata client_metadata;
        recv(client_socket, &client_metadata, sizeof(client_metadata), 0);

        if (server_metadata.mtime != client_metadata.mtime || server_metadata.size != client_metadata.size) {
            // The server's file is different from the client's file or the client's file does not exist.
            // Send the entire file to the client.
            FILE* file = fopen(FILENAME, "rb");
            if (file == NULL) {
                perror("Error opening file");
                close(client_socket);
                continue;
            }

            send_file_chunk(file, 0, server_metadata.size, client_socket, SECRET_KEY);

            // Close the file
            fclose(file);
        } else {
            // The files match, so only send the differences.
            size_t offset = 0;
            while (offset < server_metadata.size) {
                char server_chunk[1024];
                char client_chunk[1024];
                size_t chunk_size = sizeof(server_chunk);

                size_t remaining = server_metadata.size - offset;
                if (remaining < chunk_size) {
                    chunk_size = remaining;
                }

                // Read the server's chunk
                FILE* file = fopen(FILENAME, "rb");
                fseek(file, offset, SEEK_SET);
                fread(server_chunk, 1, chunk_size, file);
                fclose(file);

                // Receive the client's chunk
                recv(client_socket, client_chunk, chunk_size, 0);

                // Decrypt the server's chunk
                xor_encrypt_decrypt(server_chunk, chunk_size, SECRET_KEY);

                // Compare the chunks
                if (memcmp(server_chunk, client_chunk, chunk_size) != 0) {
                    // Send the chunk from the server to the client
                    send_file_chunk(file, offset, chunk_size, client_socket, SECRET_KEY);
                }

                offset += chunk_size;
            }
        }

        // Close the client socket
        close(client_socket);

        printf("File sent to client.\n");
    }

    // Close the server socket
    close(server_socket);

    return 0;
}
