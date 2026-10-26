// OpenGL ES 2.0 backend — iOS 6+ / armv7 (also compiles on modern SDKs for CI checks).
#import "Backend.h"

#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <vector>

#include "geometry.h"

static const char *kVS = R"GLSL(
attribute vec3 aPos;
attribute vec3 aNrm;
attribute vec3 aCol;
uniform mat4 uMVP;
varying vec3 vNrm;
varying vec3 vCol;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNrm = aNrm;
    vCol = aCol;
}
)GLSL";

static const char *kFS = R"GLSL(
precision mediump float;
varying vec3 vNrm;
varying vec3 vCol;
void main() {
    vec3 n = normalize(vNrm);
    vec3 l = normalize(vec3(0.45, 0.85, 0.55));
    float diff = max(dot(n, l), 0.0);
    vec3 c = vCol * (0.22 + 0.78 * diff);
    float rim = pow(1.0 - abs(n.z), 3.0) * 0.25;
    gl_FragColor = vec4(c + rim, 1.0);
}
)GLSL";

static GLuint compileShader(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLchar log[1024];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        NSLog(@"GLES2 shader compile failed: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

// CAEAGLLayer-backed view; owns its GL context and display link.
@interface MWEAGLView : UIView
@property(nonatomic, strong) CADisplayLink *link;
@end

@implementation MWEAGLView
+ (Class)layerClass {
    return [CAEAGLLayer class];
}
- (void)dealloc {
    [_link invalidate];
}
@end

@interface MWBackendGLES2 () {
    EAGLContext *_context;
    GLuint _framebuffer, _colorRenderbuffer, _depthRenderbuffer;
    GLuint _program;
    GLuint _cubeVBO, _voxelVBO;
    GLint _uMVP, _aPos, _aNrm, _aCol;
    GLsizei _cubeCount, _voxelCount;
    CFTimeInterval _startTime;
}
@property(nonatomic, strong) MWEAGLView *view;
@end

@implementation MWBackendGLES2

- (UIView *)makeViewWithFrame:(CGRect)frame {
    _context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    if (!_context) {
        NSLog(@"OpenGL ES 2.0 unavailable");
        return nil;
    }
    [EAGLContext setCurrentContext:_context];

    MWEAGLView *view = [[MWEAGLView alloc] initWithFrame:frame];
    CAEAGLLayer *layer = (CAEAGLLayer *)view.layer;
    layer.opaque = YES;
    layer.drawableProperties = @{
        kEAGLDrawablePropertyRetainedBacking : @NO,
        kEAGLDrawablePropertyColorFormat : kEAGLColorFormatRGBA8,
    };
    view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.view = view;

    glGenFramebuffers(1, &_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);

    glGenRenderbuffers(1, &_colorRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderbuffer);
    [_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer];
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                             _colorRenderbuffer);

    GLint width = 0, height = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
    glGenRenderbuffers(1, &_depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                             _depthRenderbuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        NSLog(@"GLES2 framebuffer incomplete");
        return view;
    }

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVS);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) return view;
    _program = glCreateProgram();
    glAttachShader(_program, vs);
    glAttachShader(_program, fs);
    glLinkProgram(_program);
    GLint linked = 0;
    glGetProgramiv(_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLchar log[1024];
        glGetProgramInfoLog(_program, sizeof(log), NULL, log);
        NSLog(@"GLES2 link failed: %s", log);
        return view;
    }
    _uMVP = glGetUniformLocation(_program, "uMVP");
    _aPos = glGetAttribLocation(_program, "aPos");
    _aNrm = glGetAttribLocation(_program, "aNrm");
    _aCol = glGetAttribLocation(_program, "aCol");

    std::vector<mw::RenderVertex> cube = mw::buildCube(0, 0, 0, 0.62f);
    _cubeCount = (GLsizei)cube.size();
    glGenBuffers(1, &_cubeVBO);
    glBindBuffer(GL_ARRAY_BUFFER, _cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(cube.size() * sizeof(mw::RenderVertex)),
                 cube.data(), GL_STATIC_DRAW);

    std::vector<mw::RenderVertex> voxels = mw::buildVoxelCluster();
    _voxelCount = (GLsizei)voxels.size();
    glGenBuffers(1, &_voxelVBO);
    glBindBuffer(GL_ARRAY_BUFFER, _voxelVBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(voxels.size() * sizeof(mw::RenderVertex)),
                 voxels.data(), GL_STATIC_DRAW);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);

    _startTime = CACurrentMediaTime();
    NSLog(@"MWBackendGLES2 ready: cube=%d voxels=%d", _cubeCount, _voxelCount);
    return view;
}

- (void)start {
    if (!_program) return;
    MWEAGLView *view = self.view;
    view.link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [view.link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)tick:(CADisplayLink *)link {
    [self renderAtTime:link.timestamp];
}

- (void)bindAttribsForBuffer:(GLuint)vbo {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const GLsizei stride = (GLsizei)sizeof(mw::RenderVertex);
    glEnableVertexAttribArray((GLuint)_aPos);
    glVertexAttribPointer((GLuint)_aPos, 3, GL_FLOAT, GL_FALSE, stride, (const void *)0);
    glEnableVertexAttribArray((GLuint)_aNrm);
    glVertexAttribPointer((GLuint)_aNrm, 3, GL_FLOAT, GL_FALSE, stride, (const void *)12);
    glEnableVertexAttribArray((GLuint)_aCol);
    glVertexAttribPointer((GLuint)_aCol, 3, GL_FLOAT, GL_FALSE, stride, (const void *)32);
}

- (void)renderAtTime:(CFTimeInterval)timestamp {
    if (!_program || !_context) return;
    [EAGLContext setCurrentContext:_context];

    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    GLint width = 0, height = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float t = (float)(timestamp - _startTime);
    const float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    mw::Mat4 proj = mw::matPerspective(60.0f * (float)M_PI / 180.0f, aspect, 0.1f, 100.0f);
    mw::Mat4 view4 = mw::matTranslation(0, 0, -4.2f);

    glUseProgram(_program);

    mw::Mat4 rot = mw::matMul(mw::matRotationY(t * 0.9f), mw::matRotationX(0.45f));
    mw::Mat4 mvp = mw::matMul(proj, mw::matMul(view4, rot));
    glUniformMatrix4fv(_uMVP, 1, GL_FALSE, mvp.m);
    [self bindAttribsForBuffer:_cubeVBO];
    glDrawArrays(GL_TRIANGLES, 0, _cubeCount);

    mw::Mat4 rot2 = mw::matMul(mw::matRotationY(-t * 0.6f), mw::matRotationX(0.25f));
    mvp = mw::matMul(proj, mw::matMul(view4, rot2));
    glUniformMatrix4fv(_uMVP, 1, GL_FALSE, mvp.m);
    [self bindAttribsForBuffer:_voxelVBO];
    glDrawArrays(GL_TRIANGLES, 0, _voxelCount);

    glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderbuffer);
    [_context presentRenderbuffer:GL_RENDERBUFFER];
}

@end
