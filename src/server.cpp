#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <fstream>
#include <sstream>
#include <zlib.h>

std::string gzip_compress(const std::string& input) {
    z_stream zs{};
    deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);

    zs.next_in = (Bytef*)input.data();
    zs.avail_in = input.size();

    std::string compressed;
    char outbuffer[32768];

    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);

        deflate(&zs, Z_FINISH);
        compressed.append(outbuffer, sizeof(outbuffer) - zs.avail_out);
    } while (zs.avail_out == 0);

    deflateEnd(&zs);
    return compressed;
}

std::string breakdown_request(std::string request, std::string target) {
    std::string user_agent_line;
    size_t pos = request.find(target);
    if (pos != std::string::npos) {
        size_t end = request.find('\n', pos);
        user_agent_line = request.substr(pos, end - pos);
    }

    std::string user_agent_value;
    const std::string key = target;
    if (!user_agent_line.empty()) {
        user_agent_value = user_agent_line.substr(key.size());
        user_agent_value.erase(0, user_agent_value.find_first_not_of(" \t"));
        if (!user_agent_value.empty() && user_agent_value.back() == '\r') {
            user_agent_value.pop_back();
        }
    }
    return user_agent_value;
}

void handle_client(int client, char **argv) {
    while (true) {
        char buffer[4096] = {0};
        int bytes_read = read(client, buffer, sizeof(buffer));
        if (bytes_read <= 0) break;

        std::string request(buffer);
        std::string method = request.substr(0, request.find(' '));
        std::string path = request.substr(request.find(' ') + 1, request.find(' ', request.find(' ') + 1) - request.find(' ') - 1);
        size_t body_pos = request.find("\r\n\r\n");
        std::string body = (body_pos != std::string::npos) ? request.substr(body_pos + 4) : "";

        std::string message;
        bool keep_alive = request.find("Connection: keep-alive") != std::string::npos;

        if (path == "/") {
            message = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n";
        } else if (path.find("/echo/") == 0) {
            std::string content_encoding_value = breakdown_request(request, "Accept-Encoding:");
            if (content_encoding_value.find("gzip") != std::string::npos) {
                std::string content = gzip_compress(path.substr(6));
                message = "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Type: text/plain\r\nContent-Length: " +
                          std::to_string(content.size()) + "\r\n\r\n" + content;
            } else {
                std::string content = path.substr(6);
                message = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                          std::to_string(content.size()) + "\r\n\r\n" + content;
            }
        } else if (path.find("/user-agent") == 0) {
            std::string user_agent_value = breakdown_request(request, "User-Agent:");
            message = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                      std::to_string(user_agent_value.size()) + "\r\n\r\n" + user_agent_value;
        } else if (path.find("/files/") == 0) {
            std::string fileName = path.substr(7);
            std::string directory = argv[2];
            std::string filePath = directory + fileName;
            if (method == "GET") {
                std::ifstream ifs(filePath);
                if (ifs.good()) {
                    std::stringstream content;
                    content << ifs.rdbuf();
                    message = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                              std::to_string(content.str().length()) + "\r\n\r\n" + content.str();
                } else {
                    message = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n\r\n";
                }
            } else if (method == "POST") {
                std::ofstream ofs(filePath);
                if (ofs.is_open()) {
                    ofs << body;
                    ofs.close();
                    message = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
                } else {
                    message = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
                }
            }
        } else {
            message = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n\r\n";
        }

        if (keep_alive) {
            message.insert(message.find("\r\n") + 2, "Connection: keep-alive\r\nKeep-Alive: timeout=5, max=1000\r\n");
        } else {
            message.insert(message.find("\r\n") + 2, "Connection: close\r\n");
        }

        send(client, message.c_str(), message.length(), 0);

        if (!keep_alive) break;
    }

    close(client);
    std::cout << "Handled client in thread\n";
}

int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "Logs from your program will appear here!\n";

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 4221\n";
        return 1;
    }

    if (listen(server_fd, 5) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    std::cout << "Waiting for a client to connect...\n";

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client >= 0) {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            std::cout << "Client connected from IP: " << client_ip << "\n";
            std::thread(handle_client, client, argv).detach();
        }
    }

    close(server_fd);
    return 0;
}
