/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License")
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifdef _WIN32
#undef  GL_APICALL
#define GL_API __declspec(dllexport)
#define GL_APICALL __declspec(dllexport)
#endif

#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

#include <OpenglCodecCommon/ErrorLog.h>
#include <GLcommon/TranslatorIfaces.h>
#include "GLESv2Context.h"
#include "GLESv2Validate.h"
#include "ShaderParser.h"
#include "ProgramData.h"
#include <GLcommon/TextureUtils.h>
#include <GLcommon/FramebufferData.h>

#include "ANGLEShaderParser.h"

#include <stdio.h>

#include <unordered_map>

extern "C" {

//decleration
static void initGLESx();
static void initContext(GLEScontext* ctx,ShareGroupPtr grp);
static void deleteGLESContext(GLEScontext* ctx);
static void setShareGroup(GLEScontext* ctx,ShareGroupPtr grp);
static GLEScontext* createGLESContext();
static __translatorMustCastToProperFunctionPointerType getProcAddress(const char* procName);

}

/************************************** GLES EXTENSIONS *********************************************************/
//extentions descriptor
typedef std::unordered_map<std::string, __translatorMustCastToProperFunctionPointerType> ProcTableMap;
ProcTableMap *s_glesExtensions = NULL;
/****************************************************************************************************************/

static EGLiface*  s_eglIface = NULL;
static GLESiface  s_glesIface = {
    .initGLESx         = initGLESx,
    .createGLESContext = createGLESContext,
    .initContext       = initContext,
    .deleteGLESContext = deleteGLESContext,
    .flush             = (FUNCPTR_NO_ARGS_RET_VOID)glFlush,
    .finish            = (FUNCPTR_NO_ARGS_RET_VOID)glFinish,
    .setShareGroup     = setShareGroup,
    .getProcAddress    = getProcAddress,
    .fenceSync         = (FUNCPTR_FENCE_SYNC)glFenceSync,
    .clientWaitSync    = (FUNCPTR_CLIENT_WAIT_SYNC)glClientWaitSync,
};

#include <GLcommon/GLESmacros.h>

extern "C" {

static void initGLESx() {
    ANGLEShaderParser::globalInitialize();
}

static void initContext(GLEScontext* ctx,ShareGroupPtr grp) {
    if (!ctx->isInitialized()) {
        ctx->setShareGroup(grp);
        ctx->init(s_eglIface->eglGetGlLibrary());
        glBindTexture(GL_TEXTURE_2D,0);
        glBindTexture(GL_TEXTURE_CUBE_MAP,0);
    }
}
static GLEScontext* createGLESContext() {
    return new GLESv2Context();
}

static void deleteGLESContext(GLEScontext* ctx) {
    delete ctx;
}

static void setShareGroup(GLEScontext* ctx,ShareGroupPtr grp) {
    if(ctx) {
        ctx->setShareGroup(grp);
    }
}

static __translatorMustCastToProperFunctionPointerType getProcAddress(const char* procName) {
    GET_CTX_RET(NULL)
    ctx->getGlobalLock();
    static bool proc_table_initialized = false;
    if (!proc_table_initialized) {
        proc_table_initialized = true;
        if (!s_glesExtensions)
            s_glesExtensions = new ProcTableMap();
        else
            s_glesExtensions->clear();
        (*s_glesExtensions)["glEGLImageTargetTexture2DOES"] = (__translatorMustCastToProperFunctionPointerType)glEGLImageTargetTexture2DOES;
        (*s_glesExtensions)["glEGLImageTargetRenderbufferStorageOES"]=(__translatorMustCastToProperFunctionPointerType)glEGLImageTargetRenderbufferStorageOES;
    }
    __translatorMustCastToProperFunctionPointerType ret=NULL;
    ProcTableMap::iterator val = s_glesExtensions->find(procName);
    if (val!=s_glesExtensions->end())
        ret = val->second;
    ctx->releaseGlobalLock();

    return ret;
}

GL_APICALL GLESiface* GL_APIENTRY __translator_getIfaces(EGLiface* eglIface);

GLESiface* __translator_getIfaces(EGLiface* eglIface) {
    s_eglIface = eglIface;
    return & s_glesIface;
}

}  // extern "C"

static void s_attachShader(GLEScontext* ctx, GLuint program, GLuint shader) {
    if (ctx && program && shader && ctx->shareGroup().get()) {
        ObjectDataPtr shaderData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        if (!shaderData.get()) return;
        ShaderParser* shaderParser = (ShaderParser*)shaderData.get();
        shaderParser->setAttachedProgram(program);
    }
}

static void s_detachShader(GLEScontext* ctx, GLuint shader) {
    if (ctx && shader && ctx->shareGroup().get()) {
        ObjectDataPtr shaderData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        if (!shaderData.get()) return;
        ShaderParser* shaderParser = (ShaderParser*)shaderData.get();
        shaderParser->setAttachedProgram(0);
        if (shaderParser->getDeleteStatus()) {
            ctx->shareGroup()->deleteName(NamedObjectType::SHADER_OR_PROGRAM, shader);
        }
    }
}

static ObjectLocalName TextureLocalName(GLenum target,unsigned int tex) {
    GET_CTX_RET(0);
    return (tex!=0? tex : ctx->getDefaultTextureName(target));
}

static TextureData* getTextureData(ObjectLocalName tex) {
    GET_CTX_RET(NULL);
    TextureData *texData = NULL;
    ObjectDataPtr objData =
            ctx->shareGroup()->getObjectData(NamedObjectType::TEXTURE, tex);
    if(!objData.get()){
        texData = new TextureData();
        ctx->shareGroup()->setObjectData(NamedObjectType::TEXTURE, tex,
                                         ObjectDataPtr(texData));
    } else {
        texData = (TextureData*)objData.get();
    }
    return texData;
}

static TextureData* getTextureTargetData(GLenum target){
    GET_CTX_RET(NULL);
    unsigned int tex = ctx->getBindedTexture(target);
    return getTextureData(TextureLocalName(target,tex));
}

GL_APICALL void  GL_APIENTRY glActiveTexture(GLenum texture){
    GET_CTX_V2();
    SET_ERROR_IF (!GLESv2Validate::textureEnum(texture,ctx->getMaxCombinedTexUnits()),GL_INVALID_ENUM);
    ctx->setActiveTexture(texture);
    ctx->dispatcher().glActiveTexture(texture);
}

GL_APICALL void  GL_APIENTRY glAttachShader(GLuint program, GLuint shader){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        const GLuint globalShaderName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(globalShaderName==0, GL_INVALID_VALUE);

        ObjectDataPtr programData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        ObjectDataPtr shaderData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(!shaderData.get() || !programData.get() ,GL_INVALID_OPERATION);
        SET_ERROR_IF(!(shaderData.get()->getDataType() ==SHADER_DATA) ||
                     !(programData.get()->getDataType()==PROGRAM_DATA) ,GL_INVALID_OPERATION);

        GLenum shaderType = ((ShaderParser*)shaderData.get())->getType();
        ProgramData* pData = (ProgramData*)programData.get();
        SET_ERROR_IF((pData->getAttachedShader(shaderType)!=0), GL_INVALID_OPERATION);
        pData->attachShader(shader,shaderType);
        s_attachShader(ctx, program, shader);
        ctx->dispatcher().glAttachShader(globalProgramName,globalShaderName);
    }
}

GL_APICALL void  GL_APIENTRY glBindAttribLocation(GLuint program, GLuint index, const GLchar* name){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::attribName(name),GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validate::attribIndex(index),GL_INVALID_VALUE);
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA,GL_INVALID_OPERATION);

        ctx->dispatcher().glBindAttribLocation(globalProgramName,index,name);
    }
}

GL_APICALL void  GL_APIENTRY glBindBuffer(GLenum target, GLuint buffer){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::bufferTarget(target),GL_INVALID_ENUM);
    //if buffer wasn't generated before,generate one
    if (buffer && ctx->shareGroup().get() &&
        !ctx->shareGroup()->isObject(NamedObjectType::VERTEXBUFFER, buffer)) {
        ctx->shareGroup()->genName(NamedObjectType::VERTEXBUFFER, buffer);
        ctx->shareGroup()->setObjectData(NamedObjectType::VERTEXBUFFER, buffer,
                                         ObjectDataPtr(new GLESbuffer()));
    }
    ctx->bindBuffer(target,buffer);
    if (buffer) {
        GLESbuffer* vbo =
                (GLESbuffer*)ctx->shareGroup()
                        ->getObjectData(NamedObjectType::VERTEXBUFFER, buffer)
                        .get();
        vbo->setBinded();
    }
}

GL_APICALL void  GL_APIENTRY glBindFramebuffer(GLenum target, GLuint framebuffer){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::framebufferTarget(target),GL_INVALID_ENUM);

    GLuint globalFrameBufferName = framebuffer;
    if(framebuffer && ctx->shareGroup().get()){
        globalFrameBufferName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::FRAMEBUFFER, framebuffer);
        //if framebuffer wasn't generated before,generate one
        if(!globalFrameBufferName){
            ctx->shareGroup()->genName(NamedObjectType::FRAMEBUFFER,
                                       framebuffer);
            ctx->shareGroup()->setObjectData(
                    NamedObjectType::FRAMEBUFFER, framebuffer,
                    ObjectDataPtr(new FramebufferData(framebuffer)));
            globalFrameBufferName = ctx->shareGroup()->getGlobalName(
                    NamedObjectType::FRAMEBUFFER, framebuffer);
        }
    }
    ctx->dispatcher().glBindFramebufferEXT(target,globalFrameBufferName);

    // update framebuffer binding state
    ctx->setFramebufferBinding(framebuffer);
}

GL_APICALL void  GL_APIENTRY glBindRenderbuffer(GLenum target, GLuint renderbuffer){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::renderbufferTarget(target),GL_INVALID_ENUM);

    GLuint globalRenderBufferName = renderbuffer;
    if(renderbuffer && ctx->shareGroup().get()){
        globalRenderBufferName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::RENDERBUFFER, renderbuffer);
        //if renderbuffer wasn't generated before,generate one
        if(!globalRenderBufferName){
            ctx->shareGroup()->genName(NamedObjectType::RENDERBUFFER,
                                       renderbuffer);
            ctx->shareGroup()->setObjectData(
                    NamedObjectType::RENDERBUFFER, renderbuffer,
                    ObjectDataPtr(new RenderbufferData()));
            globalRenderBufferName = ctx->shareGroup()->getGlobalName(
                    NamedObjectType::RENDERBUFFER, renderbuffer);
        }
    }
    ctx->dispatcher().glBindRenderbufferEXT(target,globalRenderBufferName);

    // update renderbuffer binding state
    ctx->setRenderbufferBinding(renderbuffer);
}

GL_APICALL void  GL_APIENTRY glBindTexture(GLenum target, GLuint texture){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::textureTarget(target),GL_INVALID_ENUM)

    //for handling default texture (0)
    ObjectLocalName localTexName = TextureLocalName(target,texture);
    GLuint globalTextureName = localTexName;
    if(ctx->shareGroup().get()){
        globalTextureName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::TEXTURE, localTexName);
        //if texture wasn't generated before,generate one
        if(!globalTextureName){
            ctx->shareGroup()->genName(NamedObjectType::TEXTURE, localTexName);
            globalTextureName = ctx->shareGroup()->getGlobalName(
                    NamedObjectType::TEXTURE, localTexName);
        }

        TextureData* texData = getTextureData(localTexName);
        if (texData->target==0)
            texData->target = target;
        //if texture was already bound to another target
        SET_ERROR_IF(ctx->GLTextureTargetToLocal(texData->target) != ctx->GLTextureTargetToLocal(target), GL_INVALID_OPERATION);
        texData->wasBound = true;
    }

    ctx->setBindedTexture(target,texture);
    ctx->dispatcher().glBindTexture(target,globalTextureName);
}

GL_APICALL void  GL_APIENTRY glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha){
    GET_CTX();
    ctx->dispatcher().glBlendColor(red,green,blue,alpha);
}

GL_APICALL void  GL_APIENTRY glBlendEquation( GLenum mode ){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::blendEquationMode(mode),GL_INVALID_ENUM)
    ctx->dispatcher().glBlendEquation(mode);
}

GL_APICALL void  GL_APIENTRY glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::blendEquationMode(modeRGB) && GLESv2Validate::blendEquationMode(modeAlpha)),GL_INVALID_ENUM);
    ctx->dispatcher().glBlendEquationSeparate(modeRGB,modeAlpha);
}

GL_APICALL void  GL_APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::blendSrc(sfactor) || !GLESv2Validate::blendDst(dfactor),GL_INVALID_ENUM)
    ctx->dispatcher().glBlendFunc(sfactor,dfactor);
}

GL_APICALL void  GL_APIENTRY glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha){
    GET_CTX();
    SET_ERROR_IF(
!(GLESv2Validate::blendSrc(srcRGB) && GLESv2Validate::blendDst(dstRGB) && GLESv2Validate::blendSrc(srcAlpha) && GLESv2Validate::blendDst(dstAlpha)),GL_INVALID_ENUM);
    ctx->dispatcher().glBlendFuncSeparate(srcRGB,dstRGB,srcAlpha,dstAlpha);
}

GL_APICALL void  GL_APIENTRY glBufferData(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::bufferTarget(target),GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validate::bufferUsage(usage),GL_INVALID_ENUM);
    SET_ERROR_IF(!ctx->isBindedBuffer(target),GL_INVALID_OPERATION);
    ctx->setBufferData(target,size,data,usage);
}

GL_APICALL void  GL_APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid* data){
    GET_CTX();
    SET_ERROR_IF(!ctx->isBindedBuffer(target),GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validate::bufferTarget(target),GL_INVALID_ENUM);
    SET_ERROR_IF(!ctx->setBufferSubData(target,offset,size,data),GL_INVALID_VALUE);
}


GL_APICALL GLenum GL_APIENTRY glCheckFramebufferStatus(GLenum target){
    GET_CTX_RET(GL_FRAMEBUFFER_COMPLETE);
    RET_AND_SET_ERROR_IF(!GLESv2Validate::framebufferTarget(target),GL_INVALID_ENUM,GL_FRAMEBUFFER_COMPLETE);
    ctx->drawValidate();
    return ctx->dispatcher().glCheckFramebufferStatusEXT(target);
}

GL_APICALL void  GL_APIENTRY glClear(GLbitfield mask){
    GET_CTX();
    GLbitfield allowed_bits = GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    GLbitfield has_disallowed_bits = (mask & ~allowed_bits);
    SET_ERROR_IF(has_disallowed_bits, GL_INVALID_VALUE);
    ctx->drawValidate();

    ctx->dispatcher().glClear(mask);
}
GL_APICALL void  GL_APIENTRY glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha){
    GET_CTX();
    ctx->dispatcher().glClearColor(red,green,blue,alpha);
}
GL_APICALL void  GL_APIENTRY glClearDepthf(GLclampf depth){
    GET_CTX();
    ctx->dispatcher().glClearDepth(depth);
}
GL_APICALL void  GL_APIENTRY glClearStencil(GLint s){
    GET_CTX();
    ctx->dispatcher().glClearStencil(s);
}
GL_APICALL void  GL_APIENTRY glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha){
    GET_CTX();
    ctx->dispatcher().glColorMask(red,green,blue,alpha);
}

GL_APICALL void  GL_APIENTRY glCompileShader(GLuint shader){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalShaderName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(globalShaderName==0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(objData.get()->getDataType()!= SHADER_DATA,GL_INVALID_OPERATION);
        ShaderParser* sp = (ShaderParser*)objData.get();
        if (sp->validShader()) {
            ctx->dispatcher().glCompileShader(globalShaderName);

            GLsizei infoLogLength=0;
            GLchar* infoLog;
            ctx->dispatcher().glGetShaderiv(globalShaderName,GL_INFO_LOG_LENGTH,&infoLogLength);
            infoLog = new GLchar[infoLogLength+1];
            ctx->dispatcher().glGetShaderInfoLog(globalShaderName,infoLogLength,NULL,infoLog);
            if (infoLogLength == 0) {
                infoLog[0] = 0;
            }
            sp->setInfoLog(infoLog);
        }
    }
}

GL_APICALL void  GL_APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid* data)
{
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::textureTargetEx(target),GL_INVALID_ENUM);
    SET_ERROR_IF(level < 0 || imageSize < 0, GL_INVALID_VALUE);

    doCompressedTexImage2D(ctx, target, level, internalformat,
                                width, height, border,
                                imageSize, data, (void*)glTexImage2D);
}

GL_APICALL void  GL_APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid* data){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::textureTargetEx(target),GL_INVALID_ENUM);
    SET_ERROR_IF(!data,GL_INVALID_OPERATION);
    ctx->dispatcher().glCompressedTexSubImage2D(target,level,xoffset,yoffset,width,height,format,imageSize,data);
}

GL_APICALL void  GL_APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::pixelFrmt(ctx,internalformat) && GLESv2Validate::textureTargetEx(target)),GL_INVALID_ENUM);
    SET_ERROR_IF((GLESv2Validate::textureIsCubeMap(target) && width != height), GL_INVALID_VALUE);
    SET_ERROR_IF(border != 0,GL_INVALID_VALUE);
    ctx->dispatcher().glCopyTexImage2D(target,level,internalformat,x,y,width,height,border);
}

GL_APICALL void  GL_APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::textureTargetEx(target),GL_INVALID_ENUM);
    ctx->dispatcher().glCopyTexSubImage2D(target,level,xoffset,yoffset,x,y,width,height);
}

GL_APICALL GLuint GL_APIENTRY glCreateProgram(void){
    GET_CTX_RET(0);
    if(ctx->shareGroup().get()) {
        ProgramData* programInfo = new ProgramData();
        const GLuint localProgramName =
                ctx->shareGroup()->genName(ShaderProgramType::PROGRAM, 0, true);
        ctx->shareGroup()->setObjectData(NamedObjectType::SHADER_OR_PROGRAM,
                                         localProgramName,
                                         ObjectDataPtr(programInfo));
        return localProgramName;
    }
    return 0;
}

GL_APICALL GLuint GL_APIENTRY glCreateShader(GLenum type){
    GET_CTX_V2_RET(0);
    RET_AND_SET_ERROR_IF(!GLESv2Validate::shaderType(type),GL_INVALID_ENUM,0);
    if(ctx->shareGroup().get()) {
        ShaderProgramType shaderProgramType;
        if (type == GL_VERTEX_SHADER) {
            shaderProgramType = ShaderProgramType::VERTEX_SHADER;
        } else {
            shaderProgramType = ShaderProgramType::FRAGMENT_SHADER;
        }
        const GLuint localShaderName = ctx->shareGroup()->genName(
                                                shaderProgramType, 0, true);
        ShaderParser* sp = new ShaderParser(type);
        ctx->shareGroup()->setObjectData(NamedObjectType::SHADER_OR_PROGRAM,
                                         localShaderName, ObjectDataPtr(sp));
        return localShaderName;
    }
    return 0;
}

GL_APICALL void  GL_APIENTRY glCullFace(GLenum mode){
    GET_CTX();
    ctx->dispatcher().glCullFace(mode);
}

GL_APICALL void  GL_APIENTRY glDeleteBuffers(GLsizei n, const GLuint* buffers){
    GET_CTX();
    SET_ERROR_IF(n<0,GL_INVALID_VALUE);
    if(ctx->shareGroup().get()) {
        for(int i=0; i < n; i++){
            ctx->shareGroup()->deleteName(NamedObjectType::VERTEXBUFFER,
                                          buffers[i]);
            ctx->unbindBuffer(buffers[i]);
        }
    }
}

GL_APICALL void  GL_APIENTRY glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers){
    GET_CTX();
    SET_ERROR_IF(n < 0, GL_INVALID_VALUE);
    if (ctx->shareGroup().get()) {
        for (int i = 0; i < n; i++) {
            ctx->shareGroup()->deleteName(NamedObjectType::FRAMEBUFFER,
                                          framebuffers[i]);
        }
    }
}

static void s_detachFromFramebuffer(NamedObjectType bufferType,
                                    GLuint texture) {
    GET_CTX();
    GLuint fbName = ctx->getFramebufferBinding();
    if (!fbName) return;
    ObjectDataPtr fbObj = ctx->shareGroup()->getObjectData(
            NamedObjectType::FRAMEBUFFER, fbName);
    if (fbObj.get() == NULL) return;
    FramebufferData *fbData = (FramebufferData *)fbObj.get();
    GLenum target;
    const GLenum kAttachments[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    const size_t sizen = sizeof(kAttachments)/sizeof(GLenum);
    for (size_t i = 0; i < sizen; ++i ) {
        GLuint name = fbData->getAttachment(kAttachments[i], &target, NULL);
        if (name != texture) continue;
        if (NamedObjectType::TEXTURE == bufferType &&
            GLESv2Validate::textureTargetEx(target)) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, kAttachments[i], target, 0, 0);
        } else if (NamedObjectType::RENDERBUFFER == bufferType &&
                   GLESv2Validate::renderbufferTarget(target)) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, kAttachments[i], target, 0);
        }
    }
}

GL_APICALL void  GL_APIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers){
    GET_CTX();
    SET_ERROR_IF(n<0,GL_INVALID_VALUE);
    if(ctx->shareGroup().get()) {
        for(int i=0; i < n; i++){
            ctx->shareGroup()->deleteName(NamedObjectType::RENDERBUFFER,
                                          renderbuffers[i]);
            s_detachFromFramebuffer(NamedObjectType::RENDERBUFFER,
                                    renderbuffers[i]);
        }
    }
}

GL_APICALL void  GL_APIENTRY glDeleteTextures(GLsizei n, const GLuint* textures){
    GET_CTX();
    SET_ERROR_IF(n<0,GL_INVALID_VALUE);
    if(ctx->shareGroup().get()) {
        for(int i=0; i < n; i++){
            if (textures[i]!=0) {
                if (ctx->getBindedTexture(GL_TEXTURE_2D) == textures[i])
                    ctx->setBindedTexture(GL_TEXTURE_2D,0);
                if (ctx->getBindedTexture(GL_TEXTURE_CUBE_MAP) == textures[i])
                    ctx->setBindedTexture(GL_TEXTURE_CUBE_MAP,0);
                s_detachFromFramebuffer(NamedObjectType::TEXTURE, textures[i]);
                ctx->shareGroup()->deleteName(NamedObjectType::TEXTURE,
                                              textures[i]);
            }
        }
    }
}

GL_APICALL void  GL_APIENTRY glDeleteProgram(GLuint program){
    GET_CTX();
    if(program && ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(!globalProgramName, GL_INVALID_VALUE);

        ObjectDataPtr programData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        ProgramData* pData = (ProgramData*)programData.get();
        if (pData && pData->isInUse()) {
            pData->setDeleteStatus(true);
            return;
        }
        s_detachShader(ctx, pData->getAttachedVertexShader());
        s_detachShader(ctx, pData->getAttachedFragmentShader());

        ctx->shareGroup()->deleteName(NamedObjectType::SHADER_OR_PROGRAM, program);
    }
}

GL_APICALL void  GL_APIENTRY glDeleteShader(GLuint shader){
    GET_CTX();
    if(shader && ctx->shareGroup().get()) {
        const GLuint globalShaderName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(!globalShaderName, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(!objData.get() ,GL_INVALID_OPERATION);
        SET_ERROR_IF(objData.get()->getDataType()!=SHADER_DATA,GL_INVALID_OPERATION);
        ShaderParser* sp = (ShaderParser*)objData.get();
        if (sp->getAttachedProgram()) {
            sp->setDeleteStatus(true);
        } else {
            ctx->shareGroup()->deleteName(NamedObjectType::SHADER_OR_PROGRAM, shader);
        }
    }
}

GL_APICALL void  GL_APIENTRY glDepthFunc(GLenum func){
    GET_CTX();
    ctx->dispatcher().glDepthFunc(func);
}
GL_APICALL void  GL_APIENTRY glDepthMask(GLboolean flag){
    GET_CTX();
    ctx->dispatcher().glDepthMask(flag);
}
GL_APICALL void  GL_APIENTRY glDepthRangef(GLclampf zNear, GLclampf zFar){
    GET_CTX();
    ctx->dispatcher().glDepthRange(zNear,zFar);
}

GL_APICALL void  GL_APIENTRY glDetachShader(GLuint program, GLuint shader){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        const GLuint globalShaderName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(globalShaderName==0, GL_INVALID_VALUE);

        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(!objData.get(),GL_INVALID_OPERATION);
        SET_ERROR_IF(!(objData.get()->getDataType()==PROGRAM_DATA) ,GL_INVALID_OPERATION);

        ProgramData* programData = (ProgramData*)objData.get();
        SET_ERROR_IF(!programData->isAttached(shader),GL_INVALID_OPERATION);
        programData->detachShader(shader);

        s_detachShader(ctx, shader);

        ctx->dispatcher().glDetachShader(globalProgramName,globalShaderName);
    }
}

GL_APICALL void  GL_APIENTRY glDisable(GLenum cap){
    GET_CTX();
    ctx->dispatcher().glDisable(cap);
}

GL_APICALL void  GL_APIENTRY glDisableVertexAttribArray(GLuint index){
    GET_CTX();
    SET_ERROR_IF((!GLESv2Validate::arrayIndex(ctx,index)),GL_INVALID_VALUE);
    ctx->enableArr(index,false);
    ctx->dispatcher().glDisableVertexAttribArray(index);
}

GL_APICALL void  GL_APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count){
    GET_CTX_V2();
    SET_ERROR_IF(count < 0,GL_INVALID_VALUE)
    SET_ERROR_IF(!GLESv2Validate::drawMode(mode),GL_INVALID_ENUM);

    ctx->drawValidate();

    GLESConversionArrays tmpArrs;
    ctx->setupArraysPointers(tmpArrs,first,count,0,NULL,true);

    ctx->validateAtt0PreDraw(count);

    //Enable texture generation for GL_POINTS and gl_PointSize shader variable
    //GLES2 assumes this is enabled by default, we need to set this state for GL
    if (mode==GL_POINTS) {
        ctx->dispatcher().glEnable(GL_POINT_SPRITE);
        ctx->dispatcher().glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
    }

    ctx->dispatcher().glDrawArrays(mode,first,count);

    if (mode==GL_POINTS) {
        ctx->dispatcher().glDisable(GL_VERTEX_PROGRAM_POINT_SIZE);
        ctx->dispatcher().glDisable(GL_POINT_SPRITE);
    }

    ctx->validateAtt0PostDraw();
}

GL_APICALL void  GL_APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* elementsIndices){
    GET_CTX_V2();
    SET_ERROR_IF(count < 0,GL_INVALID_VALUE)
    SET_ERROR_IF(!(GLESv2Validate::drawMode(mode) && GLESv2Validate::drawType(type)),GL_INVALID_ENUM);

    ctx->drawValidate();

    const GLvoid* indices = elementsIndices;
    if(ctx->isBindedBuffer(GL_ELEMENT_ARRAY_BUFFER)) { // if vbo is binded take the indices from the vbo
        const unsigned char* buf = static_cast<unsigned char *>(ctx->getBindedBuffer(GL_ELEMENT_ARRAY_BUFFER));
        indices = buf + SafeUIntFromPointer(elementsIndices);
    }

    GLESConversionArrays tmpArrs;
    ctx->setupArraysPointers(tmpArrs,0,count,type,indices,false);

    unsigned int maxIndex = ctx->findMaxIndex(count, type, indices);
    ctx->validateAtt0PreDraw(maxIndex);
    
    //See glDrawArrays
    if (mode==GL_POINTS) {
        ctx->dispatcher().glEnable(GL_POINT_SPRITE);
        ctx->dispatcher().glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
    }

    ctx->dispatcher().glDrawElements(mode,count,type,indices);

    if (mode==GL_POINTS) {
        ctx->dispatcher().glDisable(GL_VERTEX_PROGRAM_POINT_SIZE);
        ctx->dispatcher().glDisable(GL_POINT_SPRITE);
    }

    ctx->validateAtt0PostDraw();
}

GL_APICALL void  GL_APIENTRY glEnable(GLenum cap){
    GET_CTX();
    ctx->dispatcher().glEnable(cap);
}

GL_APICALL void  GL_APIENTRY glEnableVertexAttribArray(GLuint index){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::arrayIndex(ctx,index)),GL_INVALID_VALUE);
    ctx->enableArr(index,true);
    ctx->dispatcher().glEnableVertexAttribArray(index);
}

GL_APICALL void  GL_APIENTRY glFinish(void){
    GET_CTX();
    ctx->dispatcher().glFinish();
}
GL_APICALL void  GL_APIENTRY glFlush(void){
    GET_CTX();
    ctx->dispatcher().glFlush();
}


GL_APICALL void  GL_APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::framebufferTarget(target)              &&
                   GLESv2Validate::renderbufferTarget(renderbuffertarget) &&
                   GLESv2Validate::framebufferAttachment(attachment)),GL_INVALID_ENUM);
    SET_ERROR_IF(!ctx->shareGroup().get(), GL_INVALID_OPERATION);

    GLuint globalRenderbufferName = 0;
    ObjectDataPtr obj;

    // generate the renderbuffer object if not yet exist
    if(renderbuffer) {
        if (!ctx->shareGroup()->isObject(NamedObjectType::RENDERBUFFER,
                                         renderbuffer)) {
            ctx->shareGroup()->genName(NamedObjectType::RENDERBUFFER,
                                       renderbuffer);
            obj = ObjectDataPtr(new RenderbufferData());
            ctx->shareGroup()->setObjectData(NamedObjectType::RENDERBUFFER,
                                             renderbuffer, obj);
        }
        else {
            obj = ctx->shareGroup()->getObjectData(
                    NamedObjectType::RENDERBUFFER, renderbuffer);
        }

        globalRenderbufferName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::RENDERBUFFER, renderbuffer);
    }

    // Update the the current framebuffer object attachment state
    GLuint fbName = ctx->getFramebufferBinding();
    ObjectDataPtr fbObj = ctx->shareGroup()->getObjectData(
            NamedObjectType::FRAMEBUFFER, fbName);
    if (fbObj.get() != NULL) {
        FramebufferData *fbData = (FramebufferData *)fbObj.get();
        fbData->setAttachment(attachment, renderbuffertarget, renderbuffer, obj);
    }

    if (renderbuffer && obj.get() != NULL) {
        RenderbufferData *rbData = (RenderbufferData *)obj.get();
        if (rbData->sourceEGLImage != 0) {
            //
            // This renderbuffer object is an eglImage target
            // attach the eglimage's texture instead the renderbuffer.
            //
            ctx->dispatcher().glFramebufferTexture2DEXT(target,
                                    attachment,
                                    GL_TEXTURE_2D,
                                    rbData->eglImageGlobalTexObject->getGlobalName(),
                                    0);
            return;
        }
    }

    ctx->dispatcher().glFramebufferRenderbufferEXT(target,attachment,renderbuffertarget,globalRenderbufferName);
}

GL_APICALL void  GL_APIENTRY glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::framebufferTarget(target) &&
                   GLESv2Validate::textureTargetEx(textarget)  &&
                   GLESv2Validate::framebufferAttachment(attachment)),GL_INVALID_ENUM);
    SET_ERROR_IF(level != 0, GL_INVALID_VALUE);
    SET_ERROR_IF(!ctx->shareGroup().get(), GL_INVALID_OPERATION);

    GLuint globalTextureName = 0;

    if(texture) {
        if (!ctx->shareGroup()->isObject(NamedObjectType::TEXTURE, texture)) {
            ctx->shareGroup()->genName(NamedObjectType::TEXTURE, texture);
        }
        ObjectLocalName texname = TextureLocalName(textarget,texture);
        globalTextureName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::TEXTURE, texname);
    }

    ctx->dispatcher().glFramebufferTexture2DEXT(target,attachment,textarget,globalTextureName,level);

    // Update the the current framebuffer object attachment state
    GLuint fbName = ctx->getFramebufferBinding();
    ObjectDataPtr fbObj = ctx->shareGroup()->getObjectData(
            NamedObjectType::FRAMEBUFFER, fbName);
    if (fbObj.get() != NULL) {
        FramebufferData *fbData = (FramebufferData *)fbObj.get();
        fbData->setAttachment(attachment, textarget, 
                              texture, ObjectDataPtr());
    }
}


GL_APICALL void  GL_APIENTRY glFrontFace(GLenum mode){
    GET_CTX();
    ctx->dispatcher().glFrontFace(mode);
}

GL_APICALL void  GL_APIENTRY glGenBuffers(GLsizei n, GLuint* buffers){
    GET_CTX();
    SET_ERROR_IF(n<0,GL_INVALID_VALUE);
    if(ctx->shareGroup().get()) {
        for(int i=0; i<n ;i++) {
            buffers[i] = ctx->shareGroup()->genName(
                    NamedObjectType::VERTEXBUFFER, 0, true);
            //generating vbo object related to this buffer name
            ctx->shareGroup()->setObjectData(NamedObjectType::VERTEXBUFFER,
                                             buffers[i],
                                             ObjectDataPtr(new GLESbuffer()));
        }
    }
}

GL_APICALL void  GL_APIENTRY glGenerateMipmap(GLenum target){
    GET_CTX();
    SET_ERROR_IF(!GLESvalidate::textureTarget(target), GL_INVALID_ENUM);
    if (ctx->shareGroup().get()) {
        TextureData *texData = getTextureTargetData(target);
        if (texData) {
            unsigned int width = texData->width;
            unsigned int height = texData->height;
            // set error code if either the width or height is not a power of two.
            SET_ERROR_IF(width == 0 || height == 0 ||
                         (width & (width - 1)) != 0 || (height & (height - 1)) != 0,
                         GL_INVALID_OPERATION);
        }
    }
    ctx->dispatcher().glGenerateMipmapEXT(target);
}

GL_APICALL void  GL_APIENTRY glGenFramebuffers(GLsizei n, GLuint* framebuffers){
    GET_CTX();
    SET_ERROR_IF(n<0,GL_INVALID_VALUE);
    if(ctx->shareGroup().get()) {
        for(int i=0; i<n ;i++) {
            framebuffers[i] = ctx->shareGroup()->genName(
                    NamedObjectType::FRAMEBUFFER, 0, true);
            ctx->shareGroup()->setObjectData(
                    NamedObjectType::FRAMEBUFFER, framebuffers[i],
                    ObjectDataPtr(new FramebufferData(framebuffers[i])));
        }
    }
}

GL_APICALL void  GL_APIENTRY glGenRenderbuffers(GLsizei n, GLuint* renderbuffers){
    GET_CTX();
    SET_ERROR_IF(n<0,GL_INVALID_VALUE);
    if(ctx->shareGroup().get()) {
        for(int i=0; i<n ;i++) {
            renderbuffers[i] = ctx->shareGroup()->genName(
                    NamedObjectType::RENDERBUFFER, 0, true);
            ctx->shareGroup()->setObjectData(
                    NamedObjectType::RENDERBUFFER, renderbuffers[i],
                    ObjectDataPtr(new RenderbufferData()));
        }
    }
}

GL_APICALL void  GL_APIENTRY glGenTextures(GLsizei n, GLuint* textures){
    GET_CTX();
    SET_ERROR_IF(n<0,GL_INVALID_VALUE);
    if(ctx->shareGroup().get()) {
        for(int i=0; i<n ;i++) {
            textures[i] = ctx->shareGroup()->genName(NamedObjectType::TEXTURE,
                                                     0, true);
        }
    }
}

GL_APICALL void  GL_APIENTRY glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufsize, GLsizei* length, GLint* size, GLenum* type, GLchar* name){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA,GL_INVALID_OPERATION);
        ctx->dispatcher().glGetActiveAttrib(globalProgramName,index,bufsize,length,size,type,name);
    }
}

GL_APICALL void  GL_APIENTRY glGetActiveUniform(GLuint program, GLuint index, GLsizei bufsize, GLsizei* length, GLint* size, GLenum* type, GLchar* name){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA,GL_INVALID_OPERATION);
        ctx->dispatcher().glGetActiveUniform(globalProgramName,index,bufsize,length,size,type,name);
    }
}

GL_APICALL void  GL_APIENTRY glGetAttachedShaders(GLuint program, GLsizei maxcount, GLsizei* count, GLuint* shaders){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        ctx->dispatcher().glGetAttachedShaders(globalProgramName,maxcount,count,shaders);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA,GL_INVALID_OPERATION);
        GLint numShaders=0;
        ctx->dispatcher().glGetProgramiv(globalProgramName,GL_ATTACHED_SHADERS,&numShaders);
        for(int i=0 ; i < maxcount && i<numShaders ;i++){
            shaders[i] = ctx->shareGroup()->getLocalName(
                    NamedObjectType::SHADER_OR_PROGRAM, shaders[i]);
        }
    }
}

GL_APICALL int GL_APIENTRY glGetAttribLocation(GLuint program, const GLchar* name){
     GET_CTX_RET(-1);
     if(ctx->shareGroup().get()) {
         const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                 NamedObjectType::SHADER_OR_PROGRAM, program);
         RET_AND_SET_ERROR_IF(globalProgramName == 0, GL_INVALID_VALUE, -1);
         ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                 NamedObjectType::SHADER_OR_PROGRAM, program);
         RET_AND_SET_ERROR_IF(objData.get()->getDataType() != PROGRAM_DATA,
                              GL_INVALID_OPERATION, -1);
         ProgramData* pData = (ProgramData*)objData.get();
         RET_AND_SET_ERROR_IF(pData->getLinkStatus() != GL_TRUE,
                              GL_INVALID_OPERATION, -1);
         return ctx->dispatcher().glGetAttribLocation(globalProgramName, name);
     }
     return -1;
}

GL_APICALL void  GL_APIENTRY glGetBooleanv(GLenum pname, GLboolean* params){
    GET_CTX();

    if (ctx->glGetBooleanv(pname,params))
    {
        return;
    }

    switch(pname)
    {
        case GL_SHADER_COMPILER:
        case GL_SHADER_BINARY_FORMATS:
        case GL_NUM_SHADER_BINARY_FORMATS:
        case GL_MAX_VERTEX_UNIFORM_VECTORS:
        case GL_MAX_VARYING_VECTORS:
        case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
            if(ctx->getCaps()->GL_ARB_ES2_COMPATIBILITY)
                ctx->dispatcher().glGetBooleanv(pname,params);
            else
            {
                GLint iparam;
                glGetIntegerv(pname,&iparam);
                *params = (iparam != 0);
            }
            break;

        default:
            ctx->dispatcher().glGetBooleanv(pname,params);
    }
}

GL_APICALL void  GL_APIENTRY glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::bufferTarget(target) && GLESv2Validate::bufferParam(pname)),GL_INVALID_ENUM);
    SET_ERROR_IF(!ctx->isBindedBuffer(target),GL_INVALID_OPERATION);
    switch(pname) {
    case GL_BUFFER_SIZE:
        ctx->getBufferSize(target,params);
        break;
    case GL_BUFFER_USAGE:
        ctx->getBufferUsage(target,params);
        break;
    }
}


GL_APICALL GLenum GL_APIENTRY glGetError(void){
    GET_CTX_RET(GL_NO_ERROR)
    GLenum err = ctx->getGLerror();
    if(err != GL_NO_ERROR) {
        ctx->setGLerror(GL_NO_ERROR);
        return err;
    }
    return ctx->dispatcher().glGetError();
}

GL_APICALL void  GL_APIENTRY glGetFloatv(GLenum pname, GLfloat* params){
    GET_CTX();

    if (ctx->glGetFloatv(pname,params)) {
        return;
    }

    GLint i;

    switch (pname) {
    case GL_CURRENT_PROGRAM:
    case GL_FRAMEBUFFER_BINDING:
    case GL_RENDERBUFFER_BINDING:
        glGetIntegerv(pname,&i);
        *params = (GLfloat)i;
        break;
    case GL_NUM_COMPRESSED_TEXTURE_FORMATS:
        *params = (GLfloat)getCompressedFormats(NULL); 
        break;    
    case GL_COMPRESSED_TEXTURE_FORMATS:
        {
            int nparams = getCompressedFormats(NULL);
            if (nparams>0) {
                int * iparams = new int[nparams];
                getCompressedFormats(iparams);
                for (int i=0; i<nparams; i++) params[i] = (GLfloat)iparams[i];
                delete [] iparams;
            }
        }
        break;

    case GL_SHADER_COMPILER:
    case GL_SHADER_BINARY_FORMATS:
    case GL_NUM_SHADER_BINARY_FORMATS:
    case GL_MAX_VERTEX_UNIFORM_VECTORS:
    case GL_MAX_VARYING_VECTORS:
    case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
        if(ctx->getCaps()->GL_ARB_ES2_COMPATIBILITY)
            ctx->dispatcher().glGetFloatv(pname,params);
        else
        {
            glGetIntegerv(pname,&i);
            *params = (GLfloat)i;
        }
        break;

    case GL_STENCIL_BACK_VALUE_MASK:
    case GL_STENCIL_BACK_WRITEMASK:
    case GL_STENCIL_VALUE_MASK:
    case GL_STENCIL_WRITEMASK:
        {
            GLint myint = 0;
            glGetIntegerv(pname, &myint);
            // Two casts are used: since mask is unsigned integer,
            // the first cast converts to unsigned integer;
            // the second cast converts to float.
            *params = (GLfloat)((GLuint)(myint));
        }
        break;

    default:
        ctx->dispatcher().glGetFloatv(pname,params);
    }
}

GL_APICALL void  GL_APIENTRY glGetIntegerv(GLenum pname, GLint* params){
    int destroyCtx = 0;
    GET_CTX();

    if (!ctx) {
        ctx = createGLESContext();
        if (ctx)
            destroyCtx = 1;
    }
    if (ctx->glGetIntegerv(pname,params))
    {
        if (destroyCtx)
            deleteGLESContext(ctx);
            return;
    }
  
    bool es2 = ctx->getCaps()->GL_ARB_ES2_COMPATIBILITY;
    GLint i;

    switch (pname) {
    case GL_CURRENT_PROGRAM:
        if (ctx->shareGroup().get()) {
            ctx->dispatcher().glGetIntegerv(pname,&i);
            *params = ctx->shareGroup()->getLocalName(NamedObjectType::SHADER_OR_PROGRAM,
                                                      i);
        }
        break;
    case GL_FRAMEBUFFER_BINDING:
        if (ctx->shareGroup().get()) {
            ctx->dispatcher().glGetIntegerv(pname,&i);
            *params = ctx->shareGroup()->getLocalName(
                    NamedObjectType::FRAMEBUFFER, i);
        }
        break;
    case GL_RENDERBUFFER_BINDING:
        if (ctx->shareGroup().get()) {
            ctx->dispatcher().glGetIntegerv(pname,&i);
            *params = ctx->shareGroup()->getLocalName(
                    NamedObjectType::RENDERBUFFER, i);
        }
        break;

    case GL_NUM_COMPRESSED_TEXTURE_FORMATS:
        *params = getCompressedFormats(NULL); 
        break;    
    case GL_COMPRESSED_TEXTURE_FORMATS:
        getCompressedFormats(params);
        break;

    case GL_SHADER_COMPILER:
        if(es2)
            ctx->dispatcher().glGetIntegerv(pname,params);
        else
            *params = 1;
        break;

    case GL_SHADER_BINARY_FORMATS:
        if(es2)
            ctx->dispatcher().glGetIntegerv(pname,params);
        break;

    case GL_NUM_SHADER_BINARY_FORMATS:
        if(es2)
            ctx->dispatcher().glGetIntegerv(pname,params);
        else
            *params = 0;
        break;

    case GL_MAX_VERTEX_UNIFORM_VECTORS:
        if(es2)
            ctx->dispatcher().glGetIntegerv(pname,params);
        else
            *params = 128;
        break;

    case GL_MAX_VARYING_VECTORS:
        if(es2)
            ctx->dispatcher().glGetIntegerv(pname,params);
        else
            *params = 8;
        break;

    case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
        if(es2)
            ctx->dispatcher().glGetIntegerv(pname,params);
        else
            *params = 16;
        break;

    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
        ctx->dispatcher().glGetIntegerv(pname,params);
        break;
    default:
        ctx->dispatcher().glGetIntegerv(pname,params);
    }
    if (destroyCtx)
            deleteGLESContext(ctx);
}

GL_APICALL void  GL_APIENTRY glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint* params){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::framebufferTarget(target)         &&
                   GLESv2Validate::framebufferAttachment(attachment) &&
                   GLESv2Validate::framebufferAttachmentParams(pname)),GL_INVALID_ENUM);

    //
    // Take the attachment attribute from our state - if available
    //
    GLuint fbName = ctx->getFramebufferBinding();
    SET_ERROR_IF (!fbName, GL_INVALID_OPERATION);
    if (fbName) {
        ObjectDataPtr fbObj = ctx->shareGroup()->getObjectData(
                NamedObjectType::FRAMEBUFFER, fbName);
        if (fbObj.get() != NULL) {
            FramebufferData *fbData = (FramebufferData *)fbObj.get();
            GLenum target;
            GLuint name = fbData->getAttachment(attachment, &target, NULL);
            if (!name) {
                SET_ERROR_IF(pname != GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE &&
                        pname != GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, GL_INVALID_ENUM);
            }
            if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) {
                if (target == GL_TEXTURE_2D) {
                    *params = GL_TEXTURE;
                    return;
                }
                else if (target == GL_RENDERBUFFER) {
                    *params = GL_RENDERBUFFER;
                    return;
                } else {
                    *params = GL_NONE;
                }
            }
            else if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME) {
                *params = name;
                return;
            }
        }
    }

    ctx->dispatcher().glGetFramebufferAttachmentParameterivEXT(target,attachment,pname,params);
}

GL_APICALL void  GL_APIENTRY glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint* params){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::renderbufferTarget(target) && GLESv2Validate::renderbufferParams(pname)),GL_INVALID_ENUM);

    //
    // If this is a renderbuffer which is eglimage's target, we
    // should query the underlying eglimage's texture object instead.
    //
    GLuint rb = ctx->getRenderbufferBinding();
    if (rb) {
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::RENDERBUFFER, rb);
        RenderbufferData *rbData = (RenderbufferData *)objData.get();
        if (rbData && rbData->sourceEGLImage != 0) {
            GLenum texPname;
            switch(pname) {
                case GL_RENDERBUFFER_WIDTH:
                    texPname = GL_TEXTURE_WIDTH;
                    break;
                case GL_RENDERBUFFER_HEIGHT:
                    texPname = GL_TEXTURE_HEIGHT;
                    break;
                case GL_RENDERBUFFER_INTERNAL_FORMAT:
                    texPname = GL_TEXTURE_INTERNAL_FORMAT;
                    break;
                case GL_RENDERBUFFER_RED_SIZE:
                    texPname = GL_TEXTURE_RED_SIZE;
                    break;
                case GL_RENDERBUFFER_GREEN_SIZE:
                    texPname = GL_TEXTURE_GREEN_SIZE;
                    break;
                case GL_RENDERBUFFER_BLUE_SIZE:
                    texPname = GL_TEXTURE_BLUE_SIZE;
                    break;
                case GL_RENDERBUFFER_ALPHA_SIZE:
                    texPname = GL_TEXTURE_ALPHA_SIZE;
                    break;
                case GL_RENDERBUFFER_DEPTH_SIZE:
                    texPname = GL_TEXTURE_DEPTH_SIZE;
                    break;
                case GL_RENDERBUFFER_STENCIL_SIZE:
                default:
                    *params = 0; //XXX
                    return;
                    break;
            }

            GLint prevTex;
            ctx->dispatcher().glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
            ctx->dispatcher().glBindTexture(GL_TEXTURE_2D,
                                            rbData->eglImageGlobalTexObject->getGlobalName());
            ctx->dispatcher().glGetTexLevelParameteriv(GL_TEXTURE_2D, 0,
                                                       texPname,
                                                       params);
            ctx->dispatcher().glBindTexture(GL_TEXTURE_2D, prevTex);
            return;
        }
    }

    ctx->dispatcher().glGetRenderbufferParameterivEXT(target,pname,params);
}


GL_APICALL void  GL_APIENTRY glGetProgramiv(GLuint program, GLenum pname, GLint* params){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::programParam(pname),GL_INVALID_ENUM);
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        switch(pname) {
        case GL_DELETE_STATUS:
            {
            ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                    NamedObjectType::SHADER_OR_PROGRAM, program);
            SET_ERROR_IF(!objData.get(), GL_INVALID_OPERATION);
            SET_ERROR_IF(objData.get()->getDataType() != PROGRAM_DATA,
                         GL_INVALID_OPERATION);
            ProgramData* programData = (ProgramData*)objData.get();
            params[0] = programData->getDeleteStatus() ? GL_TRUE : GL_FALSE;
            }
            break;
        case GL_LINK_STATUS:
            {
            ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                    NamedObjectType::SHADER_OR_PROGRAM, program);
            SET_ERROR_IF(!objData.get(), GL_INVALID_OPERATION);
            SET_ERROR_IF(objData.get()->getDataType() != PROGRAM_DATA,
                         GL_INVALID_OPERATION);
            ProgramData* programData = (ProgramData*)objData.get();
            params[0] = programData->getLinkStatus();
            }
            break;
        //validate status should not return GL_TRUE if link failed
        case GL_VALIDATE_STATUS:
            {
            ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                    NamedObjectType::SHADER_OR_PROGRAM, program);
            SET_ERROR_IF(!objData.get(), GL_INVALID_OPERATION);
            SET_ERROR_IF(objData.get()->getDataType() != PROGRAM_DATA,
                         GL_INVALID_OPERATION);
            ProgramData* programData = (ProgramData*)objData.get();
            if (programData->getLinkStatus() == GL_TRUE)
                ctx->dispatcher().glGetProgramiv(globalProgramName, pname,
                                                 params);
            else
                params[0] = GL_FALSE;
            }
            break;
        case GL_INFO_LOG_LENGTH:
            {
            ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                    NamedObjectType::SHADER_OR_PROGRAM, program);
            SET_ERROR_IF(!objData.get(), GL_INVALID_OPERATION);
            SET_ERROR_IF(objData.get()->getDataType() != PROGRAM_DATA,
                         GL_INVALID_OPERATION);
            ProgramData* programData = (ProgramData*)objData.get();
            GLint logLength = strlen(programData->getInfoLog());
            params[0] = (logLength > 0) ? logLength + 1 : 0;
            }
            break;   
        default:
            ctx->dispatcher().glGetProgramiv(globalProgramName,pname,params);
        }
    }
}

GL_APICALL void  GL_APIENTRY glGetProgramInfoLog(GLuint program, GLsizei bufsize, GLsizei* length, GLchar* infolog){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(!objData.get() ,GL_INVALID_OPERATION);
        SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA,GL_INVALID_OPERATION);
        ProgramData* programData = (ProgramData*)objData.get();

        if (bufsize==0) {
            if (length) {
                *length = 0;
            }
            return;
        }

        GLsizei logLength;
        logLength = strlen(programData->getInfoLog());
        
        GLsizei returnLength=0;
        if (infolog) {
            returnLength = bufsize-1 < logLength ? bufsize-1 : logLength;
            strncpy(infolog,programData->getInfoLog(),returnLength+1);
            infolog[returnLength] = '\0';
        }
        if (length) {
            *length = returnLength;
        }
    }
}

GL_APICALL void  GL_APIENTRY glGetShaderiv(GLuint shader, GLenum pname, GLint* params){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalShaderName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        if (pname == GL_DELETE_STATUS) {
            SET_ERROR_IF(globalShaderName == 0, GL_INVALID_VALUE);
            ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                    NamedObjectType::SHADER_OR_PROGRAM, shader);
            SET_ERROR_IF(!objData.get() ,GL_INVALID_VALUE);
            SET_ERROR_IF(objData.get()->getDataType()!=SHADER_DATA,GL_INVALID_VALUE);
            ShaderParser* sp = (ShaderParser*)objData.get();
            params[0]  = (sp->getDeleteStatus()) ? GL_TRUE : GL_FALSE;
            return;
        }
        SET_ERROR_IF(globalShaderName==0, GL_INVALID_VALUE);
        switch(pname) {
        case GL_INFO_LOG_LENGTH:
            {
            ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                    NamedObjectType::SHADER_OR_PROGRAM, shader);
            SET_ERROR_IF(!objData.get(), GL_INVALID_OPERATION);
            SET_ERROR_IF(objData.get()->getDataType() != SHADER_DATA,
                         GL_INVALID_OPERATION);
            ShaderParser* sp = (ShaderParser*)objData.get();
            GLint logLength = strlen(sp->getInfoLog());
            params[0] = (logLength > 0) ? logLength + 1 : 0;
            }
            break;
        case GL_SHADER_SOURCE_LENGTH:
            {
            ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                    NamedObjectType::SHADER_OR_PROGRAM, shader);
            SET_ERROR_IF(!objData.get(), GL_INVALID_OPERATION);
            SET_ERROR_IF(objData.get()->getDataType() != SHADER_DATA,
                         GL_INVALID_OPERATION);
            ShaderParser* sp = (ShaderParser*)objData.get();
            GLint srcLength = sp->getOriginalSrc().length();
            params[0] = (srcLength > 0) ? srcLength + 1 : 0;
            }
            break;
        default:
            ctx->dispatcher().glGetShaderiv(globalShaderName,pname,params);
        }
    }
}


GL_APICALL void  GL_APIENTRY glGetShaderInfoLog(GLuint shader, GLsizei bufsize, GLsizei* length, GLchar* infolog){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalShaderName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(globalShaderName==0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(!objData.get() ,GL_INVALID_OPERATION);
        SET_ERROR_IF(objData.get()->getDataType()!=SHADER_DATA,GL_INVALID_OPERATION);
        ShaderParser* sp = (ShaderParser*)objData.get();

        if (bufsize==0) {
            if (length) {
                *length = 0;
            }
            return;
        }

        GLsizei logLength;
        logLength = strlen(sp->getInfoLog());
        
        GLsizei returnLength=0;
        if (infolog) {
            returnLength = bufsize-1 <logLength ? bufsize-1 : logLength;
            strncpy(infolog,sp->getInfoLog(),returnLength+1);
            infolog[returnLength] = '\0';
        }
        if (length) {
            *length = returnLength;
        }
    }
}

GL_APICALL void  GL_APIENTRY glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint* range, GLint* precision){
    GET_CTX_V2();
    SET_ERROR_IF(!(GLESv2Validate::shaderType(shadertype) && GLESv2Validate::precisionType(precisiontype)),GL_INVALID_ENUM);

    switch (precisiontype) {
    case GL_LOW_INT:
    case GL_MEDIUM_INT:
    case GL_HIGH_INT:
        range[0] = range[1] = 16;
        *precision = 0;
        break;

    case GL_LOW_FLOAT:
    case GL_MEDIUM_FLOAT:
    case GL_HIGH_FLOAT:
        if(ctx->dispatcher().glGetShaderPrecisionFormat != NULL) {
            ctx->dispatcher().glGetShaderPrecisionFormat(shadertype,precisiontype,range,precision);
        } else {
            range[0] = range[1] = 127;
            *precision = 24;
        }
        break;
    }
}

GL_APICALL void  GL_APIENTRY glGetShaderSource(GLuint shader, GLsizei bufsize, GLsizei* length, GLchar* source){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalShaderName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(globalShaderName == 0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(!objData.get(), GL_INVALID_OPERATION);
        SET_ERROR_IF(objData.get()->getDataType() != SHADER_DATA,
                     GL_INVALID_OPERATION);
        const std::string& src =
                ((ShaderParser*)objData.get())->getOriginalSrc();
        int srcLength = static_cast<int>(src.size());

        int returnLength = bufsize < srcLength ? bufsize - 1 : srcLength;
        if (returnLength) {
            strncpy(source, src.c_str(), returnLength);
            source[returnLength] = '\0';
       }

       if (length)
          *length = returnLength;
    }
}


GL_APICALL const GLubyte* GL_APIENTRY glGetString(GLenum name){
    GET_CTX_RET(NULL)
    static const GLubyte SHADING[] = "OpenGL ES GLSL ES 1.0.17";
    switch(name) {
        case GL_VENDOR:
            return (const GLubyte*)ctx->getVendorString();
        case GL_RENDERER:
            return (const GLubyte*)ctx->getRendererString();
        case GL_VERSION:
            return (const GLubyte*)ctx->getVersionString();
        case GL_SHADING_LANGUAGE_VERSION:
            return SHADING;
        case GL_EXTENSIONS:
            return (const GLubyte*)ctx->getExtensionString();
        default:
            RET_AND_SET_ERROR_IF(true,GL_INVALID_ENUM,NULL);
    }
}

GL_APICALL void  GL_APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::textureTarget(target) && GLESv2Validate::textureParams(pname)),GL_INVALID_ENUM);
    ctx->dispatcher().glGetTexParameterfv(target,pname,params);

}
GL_APICALL void  GL_APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint* params){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::textureTarget(target) && GLESv2Validate::textureParams(pname)),GL_INVALID_ENUM);
    ctx->dispatcher().glGetTexParameteriv(target,pname,params);
}

GL_APICALL void  GL_APIENTRY glGetUniformfv(GLuint program, GLint location, GLfloat* params){
    GET_CTX();
    SET_ERROR_IF(location < 0,GL_INVALID_OPERATION);
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA,GL_INVALID_OPERATION);
        ProgramData* pData = (ProgramData *)objData.get();
        SET_ERROR_IF(pData->getLinkStatus() != GL_TRUE,GL_INVALID_OPERATION);
        ctx->dispatcher().glGetUniformfv(globalProgramName,location,params);
    }
}

GL_APICALL void  GL_APIENTRY glGetUniformiv(GLuint program, GLint location, GLint* params){
    GET_CTX();
    SET_ERROR_IF(location < 0,GL_INVALID_OPERATION);
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA,GL_INVALID_OPERATION);
        ProgramData* pData = (ProgramData *)objData.get();
        SET_ERROR_IF(pData->getLinkStatus() != GL_TRUE,GL_INVALID_OPERATION);
        ctx->dispatcher().glGetUniformiv(globalProgramName,location,params);
    }
}

GL_APICALL int GL_APIENTRY glGetUniformLocation(GLuint program, const GLchar* name){
    GET_CTX_RET(-1);
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        RET_AND_SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE,-1);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        RET_AND_SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA,GL_INVALID_OPERATION,-1);
        ProgramData* pData = (ProgramData *)objData.get();
        RET_AND_SET_ERROR_IF(pData->getLinkStatus() != GL_TRUE,GL_INVALID_OPERATION,-1);
        return ctx->dispatcher().glGetUniformLocation(globalProgramName,name);
    }
    return -1;
}

static bool s_invalidVertexAttribIndex(GLuint index) {
    GLint param=0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &param);
    return (param < 0 || index >= (GLuint)param);
}

GL_APICALL void  GL_APIENTRY glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat* params){
    GET_CTX_V2();
    SET_ERROR_IF(s_invalidVertexAttribIndex(index), GL_INVALID_VALUE);
    const GLESpointer* p = ctx->getPointer(index);
    if(p) {
        switch(pname){
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
            *params = 0;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
            *params = p->isEnable();
            break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
            *params = p->getSize();
            break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
            *params = p->getStride();
            break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
            *params = p->getType();
            break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            *params = p->isNormalize();
            break;
        case GL_CURRENT_VERTEX_ATTRIB:
            if(index == 0)
            {
                const float* att0 = ctx->getAtt0();
                for(int i=0; i<4; i++)
                    params[i] = att0[i];
            }
            else
                ctx->dispatcher().glGetVertexAttribfv(index,pname,params);
            break;
        default:
            ctx->setGLerror(GL_INVALID_ENUM);
        }
    } else {
        ctx->setGLerror(GL_INVALID_VALUE);
    }
}

GL_APICALL void  GL_APIENTRY glGetVertexAttribiv(GLuint index, GLenum pname, GLint* params){
    GET_CTX_V2();
    SET_ERROR_IF(s_invalidVertexAttribIndex(index), GL_INVALID_VALUE);
    const GLESpointer* p = ctx->getPointer(index);
    if(p) {
        switch(pname){
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
            *params = 0;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
            *params = p->isEnable();
            break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
            *params = p->getSize();
            break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
            *params = p->getStride();
            break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
            *params = p->getType();
            break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            *params = p->isNormalize();
            break;
        case GL_CURRENT_VERTEX_ATTRIB:
            if(index == 0)
            {
                const float* att0 = ctx->getAtt0();
                for(int i=0; i<4; i++)
                    params[i] = (GLint)att0[i];
            }
            else
                ctx->dispatcher().glGetVertexAttribiv(index,pname,params);
            break;
        default:
            ctx->setGLerror(GL_INVALID_ENUM);
        }
    } else {
        ctx->setGLerror(GL_INVALID_VALUE);
    }
}

GL_APICALL void  GL_APIENTRY glGetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid** pointer){
    GET_CTX();
    SET_ERROR_IF(pname != GL_VERTEX_ATTRIB_ARRAY_POINTER,GL_INVALID_ENUM);
    SET_ERROR_IF((!GLESv2Validate::arrayIndex(ctx,index)),GL_INVALID_VALUE);

    const GLESpointer* p = ctx->getPointer(index);
    if(p) {
        *pointer = const_cast<void *>( p->getBufferData());
    } else {
        ctx->setGLerror(GL_INVALID_VALUE);
    }
}

GL_APICALL void  GL_APIENTRY glHint(GLenum target, GLenum mode){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::hintTargetMode(target,mode),GL_INVALID_ENUM);
    ctx->dispatcher().glHint(target,mode);
}

GL_APICALL GLboolean    GL_APIENTRY glIsEnabled(GLenum cap){
    GET_CTX_RET(GL_FALSE);
    RET_AND_SET_ERROR_IF(!GLESv2Validate::capability(cap),GL_INVALID_ENUM,GL_FALSE);
    return ctx->dispatcher().glIsEnabled(cap);
}

GL_APICALL GLboolean    GL_APIENTRY glIsBuffer(GLuint buffer){
    GET_CTX_RET(GL_FALSE)
    if(buffer && ctx->shareGroup().get()) {
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::VERTEXBUFFER, buffer);
        return objData.get() ? ((GLESbuffer*)objData.get())->wasBinded()
                             : GL_FALSE;
    }
    return GL_FALSE;
}

GL_APICALL GLboolean    GL_APIENTRY glIsFramebuffer(GLuint framebuffer){
    GET_CTX_RET(GL_FALSE)
    if(framebuffer && ctx->shareGroup().get()){
        return (ctx->shareGroup()->isObject(NamedObjectType::FRAMEBUFFER,
                                            framebuffer) &&
                ctx->getFramebufferBinding() == framebuffer)
                       ? GL_TRUE
                       : GL_FALSE;
    }
    return GL_FALSE;
}

GL_APICALL GLboolean    GL_APIENTRY glIsRenderbuffer(GLuint renderbuffer){
    GET_CTX_RET(GL_FALSE)
    if(renderbuffer && ctx->shareGroup().get()){
        return (ctx->shareGroup()->isObject(NamedObjectType::RENDERBUFFER,
                                            renderbuffer) &&
                ctx->getRenderbufferBinding() == renderbuffer)
                       ? GL_TRUE
                       : GL_FALSE;
    }
    return GL_FALSE;
}

GL_APICALL GLboolean    GL_APIENTRY glIsTexture(GLuint texture){
    GET_CTX_RET(GL_FALSE)
    if (texture==0)
        return GL_FALSE;
    TextureData* tex = getTextureData(texture);
    return tex ? tex->wasBound : GL_FALSE;
}

GL_APICALL GLboolean    GL_APIENTRY glIsProgram(GLuint program){
    GET_CTX_RET(GL_FALSE)
    if (program && ctx->shareGroup().get() &&
        ctx->shareGroup()->isObject(NamedObjectType::SHADER_OR_PROGRAM, program)) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        return ctx->dispatcher().glIsProgram(globalProgramName);
    }
    return GL_FALSE;
}

GL_APICALL GLboolean    GL_APIENTRY glIsShader(GLuint shader){
    GET_CTX_RET(GL_FALSE)
    if (shader && ctx->shareGroup().get() &&
        ctx->shareGroup()->isObject(NamedObjectType::SHADER_OR_PROGRAM, shader)) {
        const GLuint globalShaderName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        return ctx->dispatcher().glIsShader(globalShaderName);
    }
    return GL_FALSE;
}

GL_APICALL void  GL_APIENTRY glLineWidth(GLfloat width){
    GET_CTX();
    ctx->dispatcher().glLineWidth(width);
}

GL_APICALL void  GL_APIENTRY glLinkProgram(GLuint program){
    GET_CTX();
    GLint linkStatus = GL_FALSE;
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);

        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(!objData.get(), GL_INVALID_OPERATION);
        SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA, GL_INVALID_OPERATION);
        ProgramData* programData = (ProgramData*)objData.get();
        GLint fragmentShader   = programData->getAttachedFragmentShader();
        GLint vertexShader =  programData->getAttachedVertexShader();
        if (vertexShader != 0 && fragmentShader!=0) {
            /* validating that the fragment & vertex shaders were compiled successfuly*/
            GLint fCompileStatus = GL_FALSE;
            GLint vCompileStatus = GL_FALSE;
            GLuint fragmentShaderGlobal = ctx->shareGroup()->getGlobalName(
                    NamedObjectType::SHADER_OR_PROGRAM, fragmentShader);
            GLuint vertexShaderGlobal = ctx->shareGroup()->getGlobalName(
                    NamedObjectType::SHADER_OR_PROGRAM, vertexShader);
            ctx->dispatcher().glGetShaderiv(fragmentShaderGlobal,GL_COMPILE_STATUS,&fCompileStatus);
            ctx->dispatcher().glGetShaderiv(vertexShaderGlobal,GL_COMPILE_STATUS,&vCompileStatus);

            if(fCompileStatus != 0 && vCompileStatus != 0){
                ctx->dispatcher().glLinkProgram(globalProgramName);
                ctx->dispatcher().glGetProgramiv(globalProgramName,GL_LINK_STATUS,&linkStatus);
            }
        }
        programData->setLinkStatus(linkStatus);
        
        GLsizei infoLogLength=0;
        GLchar* infoLog;
        ctx->dispatcher().glGetProgramiv(globalProgramName,GL_INFO_LOG_LENGTH,&infoLogLength);
        infoLog = new GLchar[infoLogLength+1];
        ctx->dispatcher().glGetProgramInfoLog(globalProgramName,infoLogLength,NULL,infoLog);
        programData->setInfoLog(infoLog);
    }
}

GL_APICALL void  GL_APIENTRY glPixelStorei(GLenum pname, GLint param){
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::pixelStoreParam(pname),GL_INVALID_ENUM);
    SET_ERROR_IF(!((param==1)||(param==2)||(param==4)||(param==8)), GL_INVALID_VALUE);
    ctx->setUnpackAlignment(param);
    ctx->dispatcher().glPixelStorei(pname,param);
}

GL_APICALL void  GL_APIENTRY glPolygonOffset(GLfloat factor, GLfloat units){
    GET_CTX();
    ctx->dispatcher().glPolygonOffset(factor,units);
}

GL_APICALL void  GL_APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid* pixels){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::readPixelFrmt(format) && GLESv2Validate::pixelType(ctx,type)),GL_INVALID_ENUM);
    SET_ERROR_IF((width < 0 || height < 0),GL_INVALID_VALUE);
    SET_ERROR_IF(!(GLESv2Validate::pixelOp(format,type)),GL_INVALID_OPERATION);
    SET_ERROR_IF(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);
    ctx->dispatcher().glReadPixels(x,y,width,height,format,type,pixels);
}


GL_APICALL void  GL_APIENTRY glReleaseShaderCompiler(void){
// this function doesn't work on Mac OS with MacOSX10.9sdk
#ifndef __APPLE__

    /* Use this function with mesa will cause potential bug. Specifically,
     * calling this function between glCompileShader() and glLinkProgram() will
     * release resources that would be potentially used by glLinkProgram,
     * resulting in a segmentation fault.
     */
    const char* env = ::getenv("ANDROID_GL_LIB");
    if (env && !strcmp(env, "mesa")) {
        return;
    }

    GET_CTX();

    if(ctx->dispatcher().glReleaseShaderCompiler != NULL)
    {
        ctx->dispatcher().glReleaseShaderCompiler();
    }
#endif // !__APPLE__
}

GL_APICALL void  GL_APIENTRY glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height){
    GET_CTX();
    GLenum internal = internalformat;
    switch (internalformat) {
    case GL_RGB565:
        internal = GL_RGB;
        break;
    case GL_RGB5_A1:
        internal = GL_RGBA;
        break;
    default:
        internal = internalformat;
        break;
    }

    // Get current bounded renderbuffer
    // raise INVALID_OPERATIOn if no renderbuffer is bounded
    GLuint rb = ctx->getRenderbufferBinding();
    SET_ERROR_IF(rb == 0,GL_INVALID_OPERATION);
    ObjectDataPtr objData =
            ctx->shareGroup()->getObjectData(NamedObjectType::RENDERBUFFER, rb);
    RenderbufferData *rbData = (RenderbufferData *)objData.get();
    SET_ERROR_IF(!rbData,GL_INVALID_OPERATION);

    //
    // if the renderbuffer was an eglImage target, detach from
    // the eglImage.
    //
    if (rbData->sourceEGLImage != 0) {
        if (rbData->eglImageDetach) {
            (*rbData->eglImageDetach)(rbData->sourceEGLImage);
        }
        rbData->sourceEGLImage = 0;
        rbData->eglImageGlobalTexObject = nullptr;
    }

    ctx->dispatcher().glRenderbufferStorageEXT(target,internal,width,height);
}

GL_APICALL void  GL_APIENTRY glSampleCoverage(GLclampf value, GLboolean invert){
    GET_CTX();
    ctx->dispatcher().glSampleCoverage(value,invert);
}

GL_APICALL void  GL_APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height){
    GET_CTX();
    ctx->dispatcher().glScissor(x,y,width,height);
}

GL_APICALL void  GL_APIENTRY glShaderBinary(GLsizei n, const GLuint* shaders, GLenum binaryformat, const GLvoid* binary, GLsizei length){
    GET_CTX();

    SET_ERROR_IF( (ctx->dispatcher().glShaderBinary == NULL), GL_INVALID_OPERATION);

    if(ctx->shareGroup().get()){
        for(int i=0; i < n ; i++){
            const GLuint globalShaderName = ctx->shareGroup()->getGlobalName(
                    NamedObjectType::SHADER_OR_PROGRAM, shaders[i]);
            SET_ERROR_IF(globalShaderName == 0,GL_INVALID_VALUE);
            ctx->dispatcher().glShaderBinary(1,&globalShaderName,binaryformat,binary,length);
        }
    }
}

GL_APICALL void  GL_APIENTRY glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length){
    GET_CTX_V2();
    SET_ERROR_IF(count < 0,GL_INVALID_VALUE);
    if(ctx->shareGroup().get()){
        const GLuint globalShaderName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(globalShaderName == 0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, shader);
        SET_ERROR_IF(!objData.get(), GL_INVALID_OPERATION);
        SET_ERROR_IF(objData.get()->getDataType() != SHADER_DATA,
                     GL_INVALID_OPERATION);
        ShaderParser* sp = (ShaderParser*)objData.get();
        sp->setSrc(ctx->glslVersion(), count, string, length);
        ctx->dispatcher().glShaderSource(globalShaderName, 1, sp->parsedLines(),
                                         NULL);
        sp->clear();
    }
}

GL_APICALL void  GL_APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask){
    GET_CTX();
    ctx->dispatcher().glStencilFunc(func,ref,mask);
}
GL_APICALL void  GL_APIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask){
    GET_CTX();
    ctx->dispatcher().glStencilFuncSeparate(face,func,ref,mask);
}
GL_APICALL void  GL_APIENTRY glStencilMask(GLuint mask){
    GET_CTX();
    ctx->dispatcher().glStencilMask(mask);
}

GL_APICALL void  GL_APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask){
    GET_CTX();
    ctx->dispatcher().glStencilMaskSeparate(face,mask);
}

GL_APICALL void  GL_APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass){
    GET_CTX();
    ctx->dispatcher().glStencilOp(fail,zfail,zpass);
}

GL_APICALL void  GL_APIENTRY glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass){
    GET_CTX();
    switch (face) {
        case GL_FRONT:
        case GL_BACK:
        case GL_FRONT_AND_BACK:
            break;
        default:
            SET_ERROR_IF(1, GL_INVALID_ENUM);
    }
    ctx->dispatcher().glStencilOpSeparate(face, fail,zfail,zpass);
}

#define GL_RGBA32F                        0x8814
#define GL_RGB32F                         0x8815
GL_APICALL void  GL_APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* pixels){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::textureTargetEx(target) &&
                   GLESv2Validate::pixelFrmt(ctx,format)&&
                   GLESv2Validate::pixelType(ctx,type)),GL_INVALID_ENUM);

    SET_ERROR_IF(!GLESv2Validate::pixelItnlFrmt(ctx,internalformat), GL_INVALID_VALUE);
    SET_ERROR_IF((GLESv2Validate::textureIsCubeMap(target) && width != height), GL_INVALID_VALUE);
    SET_ERROR_IF((format == GL_DEPTH_COMPONENT || internalformat == GL_DEPTH_COMPONENT) &&
                    (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT), GL_INVALID_OPERATION);

    SET_ERROR_IF((type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT) &&
                    (format != GL_DEPTH_COMPONENT || internalformat != GL_DEPTH_COMPONENT), GL_INVALID_OPERATION);

    SET_ERROR_IF(!GLESv2Validate::pixelOp(format,type) && internalformat == ((GLint)format),GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validate::pixelSizedFrmt(ctx, internalformat, format, type), GL_INVALID_OPERATION);
    SET_ERROR_IF(border != 0,GL_INVALID_VALUE);

    if (ctx->shareGroup().get() && level == 0){
        TextureData *texData = getTextureTargetData(target);
        if(texData) {
            texData->width = width;
            texData->height = height;
            texData->border = border;
            texData->internalFormat = internalformat;
            texData->target = target;

            if (texData->sourceEGLImage != 0) {
                //
                // This texture was a target of EGLImage,
                // but now it is re-defined so we need to detach
                // from the EGLImage and re-generate global texture name
                // for it.
                //
                if (texData->eglImageDetach) {
                    (*texData->eglImageDetach)(texData->sourceEGLImage);
                }
                unsigned int tex = ctx->getBindedTexture(target);
                ctx->shareGroup()->genName(NamedObjectType::TEXTURE, tex,
                                           false);
                unsigned int globalTextureName =
                        ctx->shareGroup()->getGlobalName(
                                NamedObjectType::TEXTURE, tex);
                ctx->dispatcher().glBindTexture(GL_TEXTURE_2D,
                                                globalTextureName);
                texData->sourceEGLImage = 0;
            }
        }
    }

    if (type==GL_HALF_FLOAT_OES)
        type = GL_HALF_FLOAT_NV;
    if (pixels==NULL && type==GL_UNSIGNED_SHORT_5_5_5_1)
        type = GL_UNSIGNED_SHORT;
    if (type == GL_FLOAT)
        internalformat = (format == GL_RGBA) ? GL_RGBA32F : GL_RGB32F;
    ctx->dispatcher().glTexImage2D(target,level,internalformat,width,height,border,format,type,pixels);
}


GL_APICALL void  GL_APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::textureTarget(target) && GLESv2Validate::textureParams(pname)),GL_INVALID_ENUM);
    ctx->dispatcher().glTexParameterf(target,pname,param);
}
GL_APICALL void  GL_APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::textureTarget(target) && GLESv2Validate::textureParams(pname)),GL_INVALID_ENUM);
    ctx->dispatcher().glTexParameterfv(target,pname,params);
}
GL_APICALL void  GL_APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::textureTarget(target) && GLESv2Validate::textureParams(pname)),GL_INVALID_ENUM);
    ctx->dispatcher().glTexParameteri(target,pname,param);
}
GL_APICALL void  GL_APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint* params){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::textureTarget(target) && GLESv2Validate::textureParams(pname)),GL_INVALID_ENUM);
    ctx->dispatcher().glTexParameteriv(target,pname,params);
}

GL_APICALL void  GL_APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels){
    GET_CTX();
    SET_ERROR_IF(!(GLESv2Validate::textureTargetEx(target)), GL_INVALID_ENUM);
    // set an error if level < 0 or level > log 2 max
    SET_ERROR_IF(level < 0 || 1<<level > ctx->getMaxTexSize(), GL_INVALID_VALUE);
    SET_ERROR_IF(xoffset < 0 || yoffset < 0 || width < 0 || height < 0, GL_INVALID_VALUE);
    if (ctx->shareGroup().get()) {
        TextureData *texData = getTextureTargetData(target);
        if (texData) {
            SET_ERROR_IF(xoffset + width > (GLint)texData->width ||
                     yoffset + height > (GLint)texData->height,
                     GL_INVALID_VALUE);
        }
    }
    SET_ERROR_IF(!(GLESv2Validate::pixelFrmt(ctx,format)&&
                   GLESv2Validate::pixelType(ctx,type)),GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validate::pixelOp(format,type),GL_INVALID_OPERATION);
    SET_ERROR_IF(!pixels,GL_INVALID_OPERATION);
    if (type==GL_HALF_FLOAT_OES)
        type = GL_HALF_FLOAT_NV;

    ctx->dispatcher().glTexSubImage2D(target,level,xoffset,yoffset,width,height,format,type,pixels);

}

GL_APICALL void  GL_APIENTRY glUniform1f(GLint location, GLfloat x){
    GET_CTX();
    ctx->dispatcher().glUniform1f(location,x);
}
GL_APICALL void  GL_APIENTRY glUniform1fv(GLint location, GLsizei count, const GLfloat* v){
    GET_CTX();
    ctx->dispatcher().glUniform1fv(location,count,v);
}

GL_APICALL void  GL_APIENTRY glUniform1i(GLint location, GLint x){
    GET_CTX();
    ctx->dispatcher().glUniform1i(location,x);
}
GL_APICALL void  GL_APIENTRY glUniform1iv(GLint location, GLsizei count, const GLint* v){
    GET_CTX();
    ctx->dispatcher().glUniform1iv(location,count,v);
}
GL_APICALL void  GL_APIENTRY glUniform2f(GLint location, GLfloat x, GLfloat y){
    GET_CTX();
    ctx->dispatcher().glUniform2f(location,x,y);
}
GL_APICALL void  GL_APIENTRY glUniform2fv(GLint location, GLsizei count, const GLfloat* v){
    GET_CTX();
    ctx->dispatcher().glUniform2fv(location,count,v);
}
GL_APICALL void  GL_APIENTRY glUniform2i(GLint location, GLint x, GLint y){
    GET_CTX();
    ctx->dispatcher().glUniform2i(location,x,y);
}
GL_APICALL void  GL_APIENTRY glUniform2iv(GLint location, GLsizei count, const GLint* v){
    GET_CTX();
    ctx->dispatcher().glUniform2iv(location,count,v);
}
GL_APICALL void  GL_APIENTRY glUniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z){
    GET_CTX();
    ctx->dispatcher().glUniform3f(location,x,y,z);
}
GL_APICALL void  GL_APIENTRY glUniform3fv(GLint location, GLsizei count, const GLfloat* v){
    GET_CTX();
    ctx->dispatcher().glUniform3fv(location,count,v);
}
GL_APICALL void  GL_APIENTRY glUniform3i(GLint location, GLint x, GLint y, GLint z){
    GET_CTX();
    ctx->dispatcher().glUniform3i(location,x,y,z);
}

GL_APICALL void  GL_APIENTRY glUniform3iv(GLint location, GLsizei count, const GLint* v){
    GET_CTX();
    ctx->dispatcher().glUniform3iv(location,count,v);
}

GL_APICALL void  GL_APIENTRY glUniform4f(GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w){
    GET_CTX();
    ctx->dispatcher().glUniform4f(location,x,y,z,w);
}

GL_APICALL void  GL_APIENTRY glUniform4fv(GLint location, GLsizei count, const GLfloat* v){
    GET_CTX();
    ctx->dispatcher().glUniform4fv(location,count,v);
}

GL_APICALL void  GL_APIENTRY glUniform4i(GLint location, GLint x, GLint y, GLint z, GLint w){
    GET_CTX();
    ctx->dispatcher().glUniform4i(location,x,y,z,w);
}

GL_APICALL void  GL_APIENTRY glUniform4iv(GLint location, GLsizei count, const GLint* v){
    GET_CTX();
    ctx->dispatcher().glUniform4iv(location,count,v);
}

GL_APICALL void  GL_APIENTRY glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value){
    GET_CTX();
    SET_ERROR_IF(transpose != GL_FALSE,GL_INVALID_VALUE);
    ctx->dispatcher().glUniformMatrix2fv(location,count,transpose,value);
}

GL_APICALL void  GL_APIENTRY glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value){
    GET_CTX();
    SET_ERROR_IF(transpose != GL_FALSE,GL_INVALID_VALUE);
    ctx->dispatcher().glUniformMatrix3fv(location,count,transpose,value);
}

GL_APICALL void  GL_APIENTRY glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value){
    GET_CTX();
    SET_ERROR_IF(transpose != GL_FALSE,GL_INVALID_VALUE);
    ctx->dispatcher().glUniformMatrix4fv(location,count,transpose,value);
}

static void s_unUseCurrentProgram() {
    GET_CTX();
    GLint localCurrentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &localCurrentProgram);
    if (!localCurrentProgram) return;

    ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
            NamedObjectType::SHADER_OR_PROGRAM, localCurrentProgram);
    SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA,GL_INVALID_OPERATION);
    ProgramData* programData = (ProgramData*)objData.get();
    programData->setInUse(false);
    if (programData->getDeleteStatus()) {
        ctx->shareGroup()->deleteName(NamedObjectType::SHADER_OR_PROGRAM,
                                      localCurrentProgram);
    }
}

GL_APICALL void  GL_APIENTRY glUseProgram(GLuint program){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(program!=0 && globalProgramName==0,GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(objData.get() && (objData.get()->getDataType()!=PROGRAM_DATA),GL_INVALID_OPERATION);

        s_unUseCurrentProgram();

        ProgramData* programData = (ProgramData*)objData.get();
        if (programData) programData->setInUse(true);

        ctx->dispatcher().glUseProgram(globalProgramName);
    }
}

GL_APICALL void  GL_APIENTRY glValidateProgram(GLuint program){
    GET_CTX();
    if(ctx->shareGroup().get()) {
        const GLuint globalProgramName = ctx->shareGroup()->getGlobalName(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(globalProgramName==0, GL_INVALID_VALUE);
        ObjectDataPtr objData = ctx->shareGroup()->getObjectData(
                NamedObjectType::SHADER_OR_PROGRAM, program);
        SET_ERROR_IF(objData.get()->getDataType()!=PROGRAM_DATA,GL_INVALID_OPERATION);
        ProgramData* programData = (ProgramData*)objData.get();
        ctx->dispatcher().glValidateProgram(globalProgramName);

        GLsizei infoLogLength=0;
        GLchar* infoLog;
        ctx->dispatcher().glGetProgramiv(globalProgramName,GL_INFO_LOG_LENGTH,&infoLogLength);
        infoLog = new GLchar[infoLogLength+1];
        ctx->dispatcher().glGetProgramInfoLog(globalProgramName,infoLogLength,NULL,infoLog);
        programData->setInfoLog(infoLog);
    }
}

GL_APICALL void  GL_APIENTRY glVertexAttrib1f(GLuint indx, GLfloat x){
    GET_CTX_V2();
    ctx->dispatcher().glVertexAttrib1f(indx,x);
    if(indx == 0)
        ctx->setAttribute0value(x, 0.0, 0.0, 1.0);
}

GL_APICALL void  GL_APIENTRY glVertexAttrib1fv(GLuint indx, const GLfloat* values){
    GET_CTX_V2();
    ctx->dispatcher().glVertexAttrib1fv(indx,values);
    if(indx == 0)
        ctx->setAttribute0value(values[0], 0.0, 0.0, 1.0);
}

GL_APICALL void  GL_APIENTRY glVertexAttrib2f(GLuint indx, GLfloat x, GLfloat y){
    GET_CTX_V2();
    ctx->dispatcher().glVertexAttrib2f(indx,x,y);
    if(indx == 0)
        ctx->setAttribute0value(x, y, 0.0, 1.0);
}

GL_APICALL void  GL_APIENTRY glVertexAttrib2fv(GLuint indx, const GLfloat* values){
    GET_CTX_V2();
    ctx->dispatcher().glVertexAttrib2fv(indx,values);
    if(indx == 0)
        ctx->setAttribute0value(values[0], values[1], 0.0, 1.0);
}

GL_APICALL void  GL_APIENTRY glVertexAttrib3f(GLuint indx, GLfloat x, GLfloat y, GLfloat z){
    GET_CTX_V2();
    ctx->dispatcher().glVertexAttrib3f(indx,x,y,z);
    if(indx == 0)
        ctx->setAttribute0value(x, y, z, 1.0);
}

GL_APICALL void  GL_APIENTRY glVertexAttrib3fv(GLuint indx, const GLfloat* values){
    GET_CTX_V2();
    ctx->dispatcher().glVertexAttrib3fv(indx,values);
    if(indx == 0)
        ctx->setAttribute0value(values[0], values[1], values[2], 1.0);
}

GL_APICALL void  GL_APIENTRY glVertexAttrib4f(GLuint indx, GLfloat x, GLfloat y, GLfloat z, GLfloat w){
    GET_CTX_V2();
    ctx->dispatcher().glVertexAttrib4f(indx,x,y,z,w);
    if(indx == 0)
        ctx->setAttribute0value(x, y, z, w);
}

GL_APICALL void  GL_APIENTRY glVertexAttrib4fv(GLuint indx, const GLfloat* values){
    GET_CTX_V2();
    ctx->dispatcher().glVertexAttrib4fv(indx,values);
    if(indx == 0)
        ctx->setAttribute0value(values[0], values[1], values[2], values[3]);
}

GL_APICALL void  GL_APIENTRY glVertexAttribPointer(GLuint indx, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* ptr){
    GET_CTX();
    SET_ERROR_IF((!GLESv2Validate::arrayIndex(ctx,indx)),GL_INVALID_VALUE);
    if (type == GL_HALF_FLOAT_OES) type = GL_HALF_FLOAT;
    ctx->setPointer(indx,size,type,stride,ptr,normalized);
}

GL_APICALL void  GL_APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height){
    GET_CTX();
    ctx->dispatcher().glViewport(x,y,width,height);
}

GL_APICALL void GL_APIENTRY glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image)
{
    GET_CTX();
    SET_ERROR_IF(!GLESv2Validate::textureTargetLimited(target),GL_INVALID_ENUM);
    unsigned int imagehndl = SafeUIntFromPointer(image);
    EglImage *img = s_eglIface->eglAttachEGLImage(imagehndl);
    if (img) {
        // Create the texture object in the underlying EGL implementation,
        // flag to the OpenGL layer to skip the image creation and map the
        // current binded texture object to the existing global object.
        if (ctx->shareGroup().get()) {
            ObjectLocalName tex = TextureLocalName(target,ctx->getBindedTexture(target));
            // replace mapping and bind the new global object
            ctx->shareGroup()->replaceGlobalObject(NamedObjectType::TEXTURE, tex,
                                                   img->globalTexObj);
            ctx->dispatcher().glBindTexture(GL_TEXTURE_2D, img->globalTexObj->getGlobalName());
            TextureData *texData = getTextureTargetData(target);
            SET_ERROR_IF(texData==NULL,GL_INVALID_OPERATION);
            texData->width = img->width;
            texData->height = img->height;
            texData->border = img->border;
            texData->internalFormat = img->internalFormat;
            texData->sourceEGLImage = imagehndl;
            texData->eglImageDetach = s_eglIface->eglDetachEGLImage;
        }
    }
}

GL_APICALL void GL_APIENTRY glEGLImageTargetRenderbufferStorageOES(GLenum target, GLeglImageOES image)
{
    GET_CTX();
    SET_ERROR_IF(target != GL_RENDERBUFFER_OES,GL_INVALID_ENUM);
    unsigned int imagehndl = SafeUIntFromPointer(image);
    EglImage *img = s_eglIface->eglAttachEGLImage(imagehndl);
    SET_ERROR_IF(!img,GL_INVALID_VALUE);
    SET_ERROR_IF(!ctx->shareGroup().get(),GL_INVALID_OPERATION);

    // Get current bounded renderbuffer
    // raise INVALID_OPERATIOn if no renderbuffer is bounded
    GLuint rb = ctx->getRenderbufferBinding();
    SET_ERROR_IF(rb == 0,GL_INVALID_OPERATION);
    ObjectDataPtr objData =
            ctx->shareGroup()->getObjectData(NamedObjectType::RENDERBUFFER, rb);
    RenderbufferData *rbData = (RenderbufferData *)objData.get();
    SET_ERROR_IF(!rbData,GL_INVALID_OPERATION);

    //
    // flag in the renderbufferData that it is an eglImage target
    //
    rbData->sourceEGLImage = imagehndl;
    rbData->eglImageDetach = s_eglIface->eglDetachEGLImage;
    rbData->eglImageGlobalTexObject = img->globalTexObj;

    //
    // if the renderbuffer is attached to a framebuffer
    // change the framebuffer attachment in the undelying OpenGL
    // to point to the eglImage texture object.
    //
    if (rbData->attachedFB) {
        // update the framebuffer attachment point to the
        // underlying texture of the img
        GLuint prevFB = ctx->getFramebufferBinding();
        if (prevFB != rbData->attachedFB) {
            ctx->dispatcher().glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,
                                                   rbData->attachedFB);
        }
        ctx->dispatcher().glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
                                                    rbData->attachedPoint,
                                                    GL_TEXTURE_2D,
                                                    img->globalTexObj->getGlobalName(),
                                                    0);
        if (prevFB != rbData->attachedFB) {
            ctx->dispatcher().glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,
                                                   prevFB);
        }
    }
}

GL_APICALL GLsync GL_APIENTRY glFenceSync(GLenum condition, GLbitfield flags)
{
    GET_CTX_V2_RET(NULL);
    return ctx->dispatcher().glFenceSync(condition, flags);
}

GL_APICALL GLenum GL_APIENTRY glClientWaitSync(GLsync wait_on, GLbitfield flags, GLuint64 timeout)
{
    GET_CTX_V2_RET(GL_WAIT_FAILED);
    return ctx->dispatcher().glClientWaitSync(wait_on, flags, timeout);
}

GL_APICALL void GL_APIENTRY glWaitSync(GLsync wait_on, GLbitfield flags, GLuint64 timeout)
{
    GET_CTX_V2();
    ctx->dispatcher().glWaitSync(wait_on, flags, timeout);
}
