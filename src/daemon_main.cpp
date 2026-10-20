#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cutils/sockets.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include "storage_telemetry.h"

using namespace android::storage::guard;
namespace ab = android::base;

#define SOCKET_NAME "storage_guard"
#define FALLBACK_SOCKET_PATH "/dev/socket/storage_guard"

int ObtainServerSocket(){
    //1. try to get pre open descriptor
    int fd = android_get_control_socket(SOCKET_NAME);
    if( fd >= 0){
        LOG(INFO) << "Inherited socket descriptor: " << fd << ")";
        return fd;
    }

    // Fallback manual shell direct execution
    LOG(WARNING) << "android_get_control_socket failed, manually starting Unix socket";
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if( fd < 0){
        PLOG(FATAL) << "Failed to create a Unix Socket";
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, FALLBACK_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    unlink(FALLBACK_SOCKET_PATH);

    if(bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0){
        PLOG(FATAL) << "Manual socked bind failed";
    }

    return fd;

}

void HandleClient(int client_fd){

    char buffer[256];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if( bytes_read <= 0){
        close(client_fd);
        return;
    }

    buffer[bytes_read] = '\0';
    std::string cmd = ab::Trim(std::string(buffer));
    LOG(INFO) << "Client command received: " << cmd;

    std::string response;
    if(cmd == "CHECK_DATA"){
        response = StorageCollector::GenerateReportJson("/data");
    }else if(cmd == "CHECK_SYSTEM"){
        response = StorageCollector::GenerateReportJson("/system");
    }else if (ab::StartsWith(cmd, "CHECK")){
        std::string path = cmd.substr(6);
        response = StorageCollector::GenerateReportJson(path);
    }else{
        response = "{\"error\":\"Unknown command. Use: CHECK <path>, CHECK_DATA, CHECK_SYSTEM\"}\n";
    }
    write(client_fd, response.c_str(), response.length());
    close(client_fd);

}

int main(int argc, char** argv){
    (void)argc;
    ab::InitLogging(argv, ab::LogdLogger());
    LOG(INFO) << "Starting storage_guardd Daemon... ";

    int server_fd = ObtainServerSocket();
    if(listen(server_fd, 4) < 0){
        PLOG(FATAL) << "Socket listen failed";
    }

    LOG(INFO) << "storage_guardd ready, listening connections";
    while (true){
        int client_fd = accept(server_fd, nullptr, nullptr);
        if( client_fd < 0){
            PLOG(ERROR) << "Error accepting client";
            continue;
        }
        HandleClient(client_fd);
    }

    close(server_fd);
    return 0;

}