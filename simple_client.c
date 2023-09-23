#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define PORT 8080
#define SERVER_IP "127.0.0.1"  // Change to the server's IP address
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
void send_file_chunk(FILE* file, size_t offset, size_t size, int server_socket, char key) {
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
        send(server_socket, buffer, bytes_read, 0);

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
    int client_socket;
    struct sockaddr_in server_addr;
    struct FileMetadata server_metadata;
    char buffer[1024];

    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Initialize server address struct
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Connect to the server
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error connecting to the server");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    // Receive server's file metadata
    recv(client_socket, &server_metadata, sizeof(server_metadata), 0);

    // Get client's file metadata (if it exists)
    struct FileMetadata client_metadata;
    FILE* client_file = fopen("received_file.txt", "rb");
    if (client_file != NULL) {
        client_metadata = get_file_metadata("received_file.txt");
        fclose(client_file);
    } else {
        client_metadata.size = -1;
        client_metadata.mtime = -1;
    }

    // Send client's file metadata to the server
    send(client_socket, &client_metadata, sizeof(client_metadata), 0);

    if (server_metadata.mtime != client_metadata.mtime || server_metadata.size != client_metadata.size) {
        // The server's file is different from the client's file or the client's file does not exist.
        // Receive the entire file from the server.
        FILE* file = fopen("received_file.txt", "wb");
        if (file == NULL) {
            perror("Error opening file for writing");
            close(client_socket);
            exit(EXIT_FAILURE);
        }

        size_t total_received = 0;
        while (total_received < server_metadata.size) {
            size_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
            xor_encrypt_decrypt(buffer, bytes_received, SECRET_KEY);
            fwrite(buffer, 1, bytes_received, file);
            total_received += bytes_received;
        }

        fclose(file);
    } else {
        // The files match, so only receive the differences.
        size_t offset = 0;
        while (offset < server_metadata.size) {
            char server_chunk[1024];
            char client_chunk[1024];
            size_t chunk_size = sizeof(server_chunk);

            size_t remaining = server_metadata.size - offset;
            if (remaining < chunk_size) {
                chunk_size = remaining;
            }

            // Receive the server's chunk
            recv(client_socket, server_chunk, chunk_size, 0);

            // Decrypt the server's chunk
            xor_encrypt_decrypt(server_chunk, chunk_size, SECRET_KEY);

            // Read the client's chunk
            FILE* file = fopen("received_file.txt", "rb+");
            fseek(file, offset, SEEK_SET);
            fread(client_chunk, 1, chunk_size, file);

            // Compare the chunks
            if (memcmp(server_chunk, client_chunk, chunk_size) != 0) {
                // Write the server's chunk to the client's file
                fseek(file, offset, SEEK_SET);
                fwrite(server_chunk, 1, chunk_size, file);
            }

            fclose(file);
            offset += chunk_size;
        }
    }

    // Close the client socket
    close(client_socket);

    printf("File received and saved as 'received_file.txt'.\n");

    return 0;
}
