#ifndef _RVC_CAMERA_HPP
#define _RVC_CAMERA_HPP

#include <camera/Camera.h>
#include <camera/CameraParameters2.h>
#include <stdint.h>
#include <sys/types.h>
#include <EGL/egl.h>
#include <GLES/egl.h>
#include <GLES/gl.h>
#include <utils/threads.h>

#if PLATFORM_SDK_VERSION >= 25
  #define FOR_ANDROID_NN
#endif

namespace android {

class Surface;
class SurfaceComposerClient;
class SurfaceControl;

// ---------------------------------------------------------------------------
class RvcCamera: public Thread, public IBinder::DeathRecipient {
public:
    RvcCamera(int camW, int camH);
    virtual ~RvcCamera();
    sp<SurfaceComposerClient> session() const;
    bool getPreviewState();
private:
    virtual bool        threadLoop();
    virtual status_t    readyToRun();
    virtual void        onFirstRef();
    virtual void        binderDied(const wp<IBinder>& who);
    void openCamera();
    void initCameraParams();
    void startPreview();
    void stopPreview();
    void checkExit();
private:
    sp<SurfaceComposerClient> mSession;
    sp<Camera> mCamera;
    CameraParameters2* mCameraParams;
    sp<SurfaceControl> mFlingerSurfaceControl;
    sp<Surface> mFlingerSurface;
    bool mPreviewStarted;
    int mCamWidth;
    int mCamHeight;
};

// ---------------------------------------------------------------------------
}; // namespace android
#endif //_RVC_CAMERA_HPP
