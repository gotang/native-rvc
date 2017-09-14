#ifndef _RVC_SOCKET_HPP
#define _RVC_SOCKET_HPP

#include <stdint.h>
#include <sys/types.h>
#include <utils/Thread.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include "RvcTrack.hpp"

namespace android {

// ---------------------------------------------------------------------------

class RvcSocket: public Thread {
public:
    RvcSocket(const sp<RvcTrack>& track);
    ~RvcSocket();

    virtual bool        threadLoop();
    virtual status_t    readyToRun();
    virtual void        onFirstRef();

    void checkExit();

protected:
    int recv_msg(int fd, void *msg, int max_size);

private:
    sp<RvcTrack> mRvcTrack;
};

// ---------------------------------------------------------------------------
}; // namespace android

#endif //_RVC_SOCKET_HPP

