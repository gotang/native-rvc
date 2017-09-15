#ifndef _RVC_TRACK_HPP
#define _RVC_TRACK_HPP

#include <stdint.h>
#include <sys/types.h>
#include <utils/Mutex.h>
#include <utils/Thread.h>
#include <EGL/egl.h>
#include <GLES/egl.h>
#include <GLES/gl.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>


#ifndef PLATFORM_SDK_VERSION
  /*
    Default version is Android-KK.
    Add the following line in Android.mk to customize the version.
      LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)
  */
  #warning PLATFORM_SDK_VERSION is not defined, use 19 as default.
  #define PLATFORM_SDK_VERSION 19
#endif

#if PLATFORM_SDK_VERSION <= 19
  #define FOR_ANDROID_KK
#endif

#if PLATFORM_SDK_VERSION >= 26
  #define FOR_ANDROID_OO
#endif 

class SkPaint;
class SkPath;
class SkCanvas;
class SkBitmap;

struct TrackParams_t {
    int angle;
    int oneMeterPercent;
    int twoMeterPercent;
    int threeMeterPercent;
};

namespace android {

// ---------------------------------------------------------------------------

class Surface;
class SurfaceControl;
class SurfaceComposerClient;

class RvcTrack : public Thread, public IBinder::DeathRecipient
{
public:
    RvcTrack(TrackParams_t& params);
    virtual ~RvcTrack();
    void setTrackParams(TrackParams_t* params);
    TrackParams_t getTrackParams();
    void setAngle(int value);
    void setOneMPercent(int value);
    void setTwoMPercent(int value);
    void setThreeMPercent(int value);

    virtual bool        threadLoop();
    virtual status_t    readyToRun();
    virtual void        onFirstRef();
    virtual void        binderDied(const wp<IBinder>& who);

    void draw();
    void checkExit();

private:
    void init(int w=0, int h=0); // size of the inner bitmap
    void drawTrack(const TrackParams_t& params);
    void renderToGL(); // coordinate to the surface
    void setPointsCoordinate(const TrackParams_t& params);
    struct Point {
        double fx;
        double fy;
    };

    sp<SurfaceComposerClient> mSession;
    int mWidth;  // surface width
    int mHeight; // surface height
    int mFps;
    EGLDisplay mDisplay;
    EGLContext mContext;
    EGLSurface mSurface;
    sp<SurfaceControl> mFlingerSurfaceControl;
    sp<Surface> mFlingerSurface;
    TrackParams_t mTrackParams;
    SkPaint  *mPaint;
    SkCanvas *mCanvas;
    SkBitmap *mBitmap;
    Point     mPoint[8];
    GLuint    mTexture;
    Mutex    *mMutex;
};

// ---------------------------------------------------------------------------
}; // namespace android

#endif //_RVC_TRACK_HPP