#ifndef ENDURANCE_TEST_SHARED_EGL_CONTEXT_HPP
#define ENDURANCE_TEST_SHARED_EGL_CONTEXT_HPP

// Test-only EGL declarations keep the headless fixtures runnable with runtime
// libraries alone. Production context creation remains an engine responsibility.
#include "BackgroundShaderCompiler.hpp"
#include <stdexcept>

extern "C"
{
    void* eglGetCurrentDisplay();
    void* eglGetCurrentContext();
    unsigned int eglQueryContext(void*,void*,int,int*);
    unsigned int eglChooseConfig(void*,const int*,void**,int,int*);
    void* eglCreateContext(void*,void*,void*,const int*);
    unsigned int eglDestroyContext(void*,void*);
    unsigned int eglBindAPI(unsigned int);
    unsigned int eglMakeCurrent(void*,void*,void*,void*);
}

// Construct on the caller's thread while its EGL context is current. This
// object must outlive the mesher/compiler and their shutdown/join operations.
class SharedEglContext
{
public:
    SharedEglContext()
    {
        display_=eglGetCurrentDisplay();
        const auto shared=eglGetCurrentContext();
        if(!display_ || !shared) throw std::runtime_error("No current EGL context to share");
        int configId=0;
        if(!eglQueryContext(display_,shared,0x3028,&configId))
            throw std::runtime_error("Cannot query EGL context configuration");
        const int configAttributes[]={0x3028,configId,0x3038};
        void* configuration=nullptr;
        int count=0;
        if(!eglChooseConfig(display_,configAttributes,&configuration,1,&count) || count!=1)
            throw std::runtime_error("Cannot find EGL configuration for shared context");
        const int contextAttributes[]={0x3098,4,0x30fb,5,0x30fd,1,0x3038};
        context_=eglCreateContext(display_,configuration,shared,contextAttributes);
        if(!context_) throw std::runtime_error("Cannot create shared EGL compiler context");
    }
    ~SharedEglContext()
    {
        if(context_) eglDestroyContext(display_,context_);
    }
    SharedEglContext(const SharedEglContext&)=delete;
    SharedEglContext& operator=(const SharedEglContext&)=delete;
    Ge::BackgroundShaderCompiler::ContextHooks hooks()
    {
        return {
            [this]() { return eglBindAPI(0x30a2) && eglMakeCurrent(display_,nullptr,nullptr,context_); },
            [this]() { eglMakeCurrent(display_,nullptr,nullptr,nullptr); }
        };
    }
private:
    void* display_=nullptr;
    void* context_=nullptr;
};
#endif
