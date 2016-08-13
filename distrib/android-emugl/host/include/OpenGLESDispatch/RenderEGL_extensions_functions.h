// Auto-generated with: android/scripts/gen-entries.py --mode=functions distrib/android-emugl/host/libs/libOpenGLESDispatch/render_egl_extensions.entries --output=distrib/android-emugl/host/include/OpenGLESDispatch/RenderEGL_extensions_functions.h
// DO NOT EDIT THIS FILE

#ifndef RENDER_EGL_EXTENSIONS_FUNCTIONS_H
#define RENDER_EGL_EXTENSIONS_FUNCTIONS_H

#include <EGL/egl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/eglext.h>
#define LIST_RENDER_EGL_EXTENSIONS_FUNCTIONS(X) \
  X(EGLImageKHR, eglCreateImageKHR, (EGLDisplay display, EGLContext context, EGLenum target, EGLClientBuffer buffer, const EGLint* attrib_list)) \
  X(EGLBoolean, eglDestroyImageKHR, (EGLDisplay display, EGLImageKHR image)) \
  X(EGLSyncKHR, eglCreateSyncKHR, (EGLDisplay display, EGLenum type, const EGLint* attribs)) \
  X(EGLint, eglClientWaitSyncKHR, (EGLDisplay display, EGLSyncKHR sync, EGLint flags, EGLTimeKHR timeout)) \


#endif  // RENDER_EGL_EXTENSIONS_FUNCTIONS_H
