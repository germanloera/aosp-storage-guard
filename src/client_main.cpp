#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cutils/sockets.h>
#include <android-base/logging.h>

#define SOCKET_PATH "/dev/socket/storage_guard"

namespace ab = android::base;

int main(int argc, char **argv) {
    ab::InitLogging(argv, ab::LogdLogger());
    std::string cmd = (argc > 1) ? argv[1] : "CHECK_DATA";

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Error creating local socket";
        return 1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Unable to connect with storage_guardd daemon on " << SOCKET_PATH
                  << ". Is the service running?";
        close(sock);
        return 1;
    }

    write(sock, cmd.c_str(), cmd.length());

    char buffer[1024];
    ssize_t  n = read(sock, buffer, sizeof(buffer) - 1);
    if(n >0){
        buffer[n]= '\0';
        std::cout << "[Daemon Response]: \n" << buffer;
    }
    close(sock);
    return 0;
}