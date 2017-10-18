#define LOG_NDEBUG 0
#define LOG_TAG "RvcTrackReceiver"

#include <sys/socket.h>
#include <sys/un.h>
#include <utils/Errors.h>
#include <utils/Log.h>
#include <utils/threads.h>
#include <cutils/properties.h>

#include "RvcTrackReceiver.hpp"

#define UNIX_DOMAIN "/data/user/0/nativervctrack.domain"
#define RVC_EXIT_PROP "usr.rvc.exit"
#define TRACK_PARAMS_AGRC 4

enum {
    RVC_CMD_OK,
    RVC_CMD_ERR_NOCONNECT,
    RVC_CMD_ERR_TIMEOUT,
    RVC_CMD_ERR_OTHER,
};

namespace android {

// ---------------------------------------------------------------------------
RvcTrackReceiver::RvcTrackReceiver(const sp<RvcTrack>& track): Thread(false), mRvcTrack(track) {
    ALOGD("RvcTrackReceiver::RvcTrackReceiver()");
}

RvcTrackReceiver::~RvcTrackReceiver() {
}

void RvcTrackReceiver::onFirstRef() {
    ALOGD("RvcTrackReceiver::onFirstRef()");
    run("RvcTrackReceiver", PRIORITY_BACKGROUND);
}

status_t RvcTrackReceiver::readyToRun() {
    ALOGD("RvcTrackReceiver::readyToRun()");
    return NO_ERROR;
}

bool RvcTrackReceiver::threadLoop() {
    int socketfd = -1;
    while(!exitPending()) {
        unlink(UNIX_DOMAIN);
        int server_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un server_addr,client_addr;
        socklen_t len = sizeof(client_addr);
        server_addr.sun_family = AF_UNIX;
        strncpy(server_addr.sun_path, UNIX_DOMAIN, strlen(UNIX_DOMAIN));
        bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        listen(server_sockfd, 5);
        socketfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &len);
        if (socketfd < 0) {
			continue;
		} else {
			ALOGD("Successfully connected to client!");
		}
        while(!exitPending()) {
            char msg[256] = {0};
            int numbytes = recv_msg(socketfd, &msg, sizeof(msg));    
            if (numbytes == -RVC_CMD_ERR_TIMEOUT) {
                continue;
            } else if (numbytes <= 0) {
                ALOGE("socket is broken, re-connect.");
                break;
            }
            int value = 0;
            switch(msg[0]) {
                case 'l':
                case 'r':
                    value = atoi(&msg[2]);
                    mRvcTrack->setDynamicTrackParams(msg[0], value);
                    break;
                case 'L':
                case 'R':
                    mRvcTrack->adjustDynamicTrackParams(msg[0], msg[1]);
                    break;
                default:
                    break;
            };
            checkExit();
        }
        close(socketfd);
    }
    IPCThreadState::self()->stopProcess();
    return false;
}

void RvcTrackReceiver::checkExit() {
    char value[PROPERTY_VALUE_MAX];
    property_get(RVC_EXIT_PROP, value, "0");
    int exitnow = atoi(value);
    if (exitnow) {
        requestExit();
    }
}

int RvcTrackReceiver::recv_msg(int fd, void *msg, int max_size)
{
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(fd, &readfd);
	struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;

	int retval = select((fd+1), &readfd, NULL, NULL, &tv);
	if (retval == -1) {
		ALOGE("select() return error: %s", strerror(errno));
		return -RVC_CMD_ERR_NOCONNECT;
	} else if (retval == 0) {
		ALOGD("select() timeout");
		return -RVC_CMD_ERR_TIMEOUT;
	} else {
		//ALOGD("Data is available now");
	}

	int n = recv(fd, msg, max_size, 0);
	if (n == -1) {
		ALOGE("recv() return error: %s", strerror(errno));
		if (errno==EAGAIN || errno==EWOULDBLOCK) {
			return -RVC_CMD_ERR_TIMEOUT;
		}
		if (errno==ECONNREFUSED || errno==ENOTCONN || errno==ENOTSOCK) {
			return -RVC_CMD_ERR_NOCONNECT;
		}
		return -RVC_CMD_ERR_OTHER;
	} else if (n == 0) {
		ALOGE("recv() return error: %s", strerror(errno));
		if (errno==ECONNREFUSED || errno==ENOTCONN || errno==ENOTSOCK || errno==ENOENT) {
			return -RVC_CMD_ERR_NOCONNECT;
		}
	}
	return n;
}

// ---------------------------------------------------------------------------

}
; // namespace android

